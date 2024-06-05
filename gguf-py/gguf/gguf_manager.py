from __future__ import annotations

import os
from enum import IntEnum
from typing import TYPE_CHECKING, Any, Sequence, Mapping
from string import ascii_letters, digits
from argparse import Namespace
from math import ceil
from collections import deque

import numpy as np

if TYPE_CHECKING:
    from typing_extensions import TypeAlias

from .constants import (
    GGMLQuantizationType,
    GGUFEndian,
    GGUFValueType
)
from .gguf_writer import GGUFWriter


SHARD_NAME_FORMAT = "{:s}-{:05d}-of-{:05d}.gguf"

LLM_KV_SPLIT_NO = "split.no"
LLM_KV_SPLIT_COUNT = "split.count"
LLM_KV_SPLIT_TENSORS_COUNT = "split.tensors.count"

SplitTensorsPerFile: TypeAlias = deque[tuple[os.PathLike[str], deque[tuple[str, Any]], GGUFWriter]] # [(outfile name, [(tensor name, tensor data)] for each tensor in file, filewriter)]
KVTempData: TypeAlias = dict[str, tuple[Any, GGUFValueType]] # {key: (value, type)}
TensorTempData: TypeAlias = tuple[str, np.ndarray[Any, Any], GGMLQuantizationType] # (tensor name, tensor data, tensor dtype)


class SplitStyle(IntEnum):
    NONE = 0
    TENSORS = 1
    SIZE = 2


class SplitArguments:
    def __init__(self, args: Namespace = None) -> None:
        self.split = args.split if args else False
        self.split_max_tensors = args.split_max_tensors if args else 0
        self.split_max_size = SplitStrategy.split_str_to_n_bytes(args.split_max_size) if args and args.split_max_size else 0
        self.dry_run = args.dry_run if args else False
        self.small_first_shard = not args.large_first_shard if args else False
        self.split_style = SplitStyle.NONE if not self.split or not args \
            else SplitStyle.TENSORS if self.split_max_tensors \
            else SplitStyle.SIZE


class SplitStrategy(deque):
    data: SplitTensorsPerFile

    def __init__(self, fname_out: os.PathLike[str], model: list[TensorTempData], arch: str,
                 split_arguments: SplitArguments, use_temp_file: bool = True, endianess: GGUFEndian = GGUFEndian.LITTLE,
    ):
        super().__init__()

        if split_arguments.split_style == SplitStyle.NONE:
            self.append((fname_out, model, GGUFWriter(fname_out, arch, use_temp_file=use_temp_file, endianess=endianess)))

        elif split_arguments.split_style == SplitStyle.TENSORS:
            total_shards = ceil(len(model) / split_arguments.split_max_tensors) + split_arguments.small_first_shard
            shard_files = [fname_out.with_name(SHARD_NAME_FORMAT.format(fname_out.stem, i + 1, total_shards)) for i in range(total_shards)]

            if split_arguments.small_first_shard:
                self.append((shard_files[0], None, GGUFWriter(shard_files[0], arch, use_temp_file=use_temp_file, endianess=endianess)))

            for i, shard in enumerate(shard_files[split_arguments.small_first_shard:]):
                start = i * split_arguments.split_max_tensors
                stop = min((i + 1) * split_arguments.split_max_tensors, len(model))
                self.append((shard, model[start:stop], GGUFWriter(shard, arch, use_temp_file=use_temp_file, endianess=endianess)))

        elif split_arguments.split_style == SplitStyle.SIZE:
            shards = deque()

            # we have to determine the shards first to determine how many shards there will be in total - two passes
            for i, shard in enumerate(model):
                if i == 0:
                    shards.append([shard])
                    continue
                if SplitStrategy.get_tensor_size(shard[1]) + sum(SplitStrategy.get_tensor_size(t[1]) for t in shards[-1]) > split_arguments.split_max_size:
                    shards.append([shard])
                else:
                    shards[-1].append(shard)

            total_shards = len(shards) + split_arguments.small_first_shard
            shard_offset = 1

            if split_arguments.small_first_shard:
                outname = fname_out.with_name(SHARD_NAME_FORMAT.format(fname_out.stem, shard_offset, total_shards))
                self.append((outname, None, GGUFWriter(outname, arch, use_temp_file=use_temp_file, endianess=endianess)))
                shard_offset += 1

            for i, shard in enumerate(shards):
                outname = fname_out.with_name(SHARD_NAME_FORMAT.format(fname_out.stem, i + shard_offset, total_shards))
                self.append((outname, shard, GGUFWriter(outname, arch, use_temp_file=use_temp_file, endianess=endianess)))

    @staticmethod
    def get_tensor_size(tensor) -> int:
        # we don't have the LazyTensor class here from convert.py but we can try
        try:
            return tensor.data_type.elements_to_bytes(np.prod(tensor.shape))
        except AttributeError: # numpy ndarray[Any, Any]
            return tensor.nbytes
        except: # this should never happen
            raise ValueError(f"Invalid tensor type: {type(tensor)}")
    
    @staticmethod
    def split_str_to_n_bytes(split_str: str) -> int:
        if split_str.endswith("K"):
            n = int(split_str[:-1]) * 1024
        elif split_str.endswith("M"):
            n = int(split_str[:-1]) * 1024 * 1024
        elif split_str.endswith("G"):
            n = int(split_str[:-1]) * 1024 * 1024 * 1024
        elif split_str.isnumeric():
            n = int(split_str)
        else:
            raise ValueError(f"Invalid split size: {split_str}, must be a number, optionally followed by K, M, or G")

        if n <= 0:
            raise ValueError(f"Invalid split size: {split_str}, must be positive")

        return n

    @staticmethod
    def format_n_bytes_to_str(num: int) -> str:
        num = float(num)
        for unit in ("", "K", "M", "G"):
            if abs(num) < 1024.0:
                return f"{num:3.1f}{unit}"
            num /= 1024.0
        return f"{num:.1f}T - over 1TB, --split recommended"

# TODO fall back to normal GGUFWriter in convert-hf-to-gguf.py if no --split
class GGUFManager(GGUFWriter):
    kv_data: KVTempData
    tensors: list[TensorTempData]
    split_arguments: SplitArguments
    split_strategy: SplitStrategy

    def __init__(self, path: os.PathLike[str] | str, arch: str, split_arguments: SplitArguments,
                 use_temp_file: bool = True, endianess: GGUFEndian = GGUFEndian.LITTLE
        ) -> None:
        # TODO be able to use superclass constructor
        # super().__init__(path, arch, use_temp_file=use_temp_file, endianess=endianess)
        self.arch = arch
        self.path = path
        self.endianess = endianess
        self.offset_tensor = 0
        self.kv_data = {}
        self.tensors = []
        # TODO how many of these do you need
        self.split_strategy = None
        self.total_shards = None
        self.total_tensors = None
        self.use_temp_file = use_temp_file
        self.split_arguments = split_arguments
        self.recent_key = None
        self.add_architecture()

    # TODO split back into write_header_to_file, write_kv_data_to_file, write_ti_data_to_file
    def write_to_file(self, meta_only: bool = False) -> None:

        # here is the first place you can assume you have all tensors written and you can establish the size of the file - so logic goes here
        self.total_tensors = len(self.tensors)
        total_size = sum(SplitStrategy.get_tensor_size(tensor[1]) for tensor in self.tensors)

        if self.split_arguments.split_max_tensors and self.total_tensors < self.split_arguments.split_max_tensors:
            print("Model has fewer tensors than the split threshold, not splitting")
            self.split_style = SplitStyle.NONE

        if self.split_arguments.split_max_size and total_size < self.split_arguments.split_max_size:
            print("Model has smaller size than the split threshold, not splitting")
            self.split_style = SplitStyle.NONE

        self.split_strategy = SplitStrategy(self.path, self.tensors, self.arch, self.split_arguments,
                                            use_temp_file=self.use_temp_file, endianess=self.endianess)
        del self.tensors
        self.total_shards = len(self.split_strategy)

        # only the first shard needs all the KV data
        for key, (value, etype) in self.kv_data.items():
            self.split_strategy[0][2].add_key(key)
            self.split_strategy[0][2].add_val(value, etype)

        if self.split_arguments.split_style != SplitStyle.NONE:
            for i, (_, _, writer) in enumerate(self.split_strategy):
                writer.add_uint16(LLM_KV_SPLIT_NO, i)
                writer.add_uint16(LLM_KV_SPLIT_COUNT, self.total_shards)
                writer.add_int32(LLM_KV_SPLIT_TENSORS_COUNT, self.total_tensors)

        # metadata/vocab only can write and return here
        if meta_only:
            for i, (_, _, writer) in enumerate(self.split_strategy):
                writer.write_header_to_file()
                writer.write_kv_data_to_file()
            return
        
        # tensor writing code starts here

        print("\nWriting the following files:")
        for (shard_path, shard_tensors, _) in self.split_strategy:
            size = SplitStrategy.format_n_bytes_to_str(sum(SplitStrategy.get_tensor_size(t[1]) for t in shard_tensors)) if shard_tensors else "negligible - metadata only"
            print(f"  {shard_path}: n_tensors = {len(shard_tensors) if shard_tensors else 0}, total_size = {size}")

        if self.split_arguments.dry_run:
            print("\nDry run, not writing files")
            # instantiating GGUFWriters creates files
            for name, _, _ in self.split_strategy:
                os.remove(name)
            return

        # run add_tensor_info, write data, then write_tensor_data - taken from convert.py
        running_total = self.total_tensors
        ct = 0
        while True:
            try:
                (_, tensors, writer) = self.split_strategy.popleft()
                tensors = deque(tensors) if tensors else None
            except IndexError:
                break

            shard_num_tensors = len(tensors) if tensors else 0

            if tensors:
                while True:
                    try:
                        (name, tensor, dtype) = tensors.popleft()
                    except IndexError:
                        break
                    writer.add_tensor(name, tensor, raw_dtype=dtype)

                print(f"Writing to shard {ct + 1}/{self.total_shards} with {shard_num_tensors}/{running_total} remaining tensors (of {self.total_tensors} total)")
                running_total -= shard_num_tensors

            writer.write_header_to_file()
            writer.write_kv_data_to_file()
            writer.write_tensors_to_file(progress=True)
            ct = ct + 1
            del tensors

    # override add_key, add_val to handle kv data separately
    def add_key(self, key: str) -> None:
        self.recent_key = key
    
    def add_val(self, val: Any, vtype: GGUFValueType | None = None, add_vtype: bool = True) -> None:
        if self.recent_key is None:
            raise ValueError("No key set for value")
        self.kv_data[self.recent_key] = (val, vtype)

    # need to handle arrays separately
    def add_array(self, key: str, val: Sequence[Any]) -> None:
        if not isinstance(val, Sequence):
            raise ValueError(f'Expected a sequence for {key}, got {type(val)}')
        self.kv_data[key] = (val, GGUFValueType.ARRAY)

    def add_tensor(
        self, name: str, tensor: np.ndarray[Any, Any], raw_shape: Sequence[int] | None = None,
        raw_dtype: GGMLQuantizationType | None = None,
    ) -> None:
        if self.endianess == GGUFEndian.BIG:
            tensor.byteswap(inplace=True)
        self.tensors.append((name, tensor, raw_dtype))

    def close(self) -> None:
        for _, _, writer in self.split_strategy:
            writer.close()