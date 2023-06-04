#include "ggml.h"
#include "ggml-mtl.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <vector> // tmp

int main(int argc, char ** argv) {
    ggml_time_init();

    if (argc != 2) {
        fprintf(stderr, "Usage: %s llama.ggml\n", argv[0]);
        return -1;
    }

    const char * fname_cgraph = argv[1];

    // load the compute graph
    struct ggml_context * ctx_data = NULL;
    struct ggml_context * ctx_eval = NULL;

    struct ggml_cgraph gf = ggml_graph_import(fname_cgraph, &ctx_data, &ctx_eval);
    gf.n_threads = 1;

    int32_t n_vocab = 0;

    {
        struct ggml_tensor * t_vocab = ggml_graph_get_tensor(&gf, "vocab");
        if (t_vocab == NULL) {
            fprintf(stderr, "%s: vocab tensor not found\n", __func__);
            return -1;
        }

        const char * ptr = (const char *) t_vocab->data;

        memcpy(&n_vocab, ptr, sizeof(n_vocab)); ptr += sizeof(n_vocab);

        printf("%s: n_vocab = %d\n", __func__, n_vocab);

        for (int i = 0; i < 512; ++i) {
            char text[32];
            float score;

            memcpy(text,   ptr, sizeof(text));  ptr += sizeof(text);
            memcpy(&score, ptr, sizeof(score)); ptr += sizeof(score);

            printf("%s: token[%4d] = %16.*s, score = %6.2f\n", __func__, i, (int) sizeof(text), text, score);
        }
    }

    // this allocates all Metal resources and memory buffers
    auto * ctx_mtl = ggml_mtl_init();

    ggml_mtl_add_buffer(ctx_mtl, "data", ggml_get_mem_buffer(ctx_data), ggml_get_mem_size(ctx_data));
    ggml_mtl_add_buffer(ctx_mtl, "eval", ggml_get_mem_buffer(ctx_eval), ggml_get_mem_size(ctx_eval));

    // TODO: tmp to match the input used when creating the cgraph
    {
        const std::vector<int> tmp(1, 1); // BOS

        struct ggml_tensor * input = ggml_graph_get_tensor(&gf, "embd");
        memcpy(input->data, tmp.data(), tmp.size() * sizeof(int));

        ggml_mtl_set_tensor(ctx_mtl, input);

        // warmup
        ggml_mtl_graph_compute(ctx_mtl, &gf);

        const int n_iter = 16;

        const int64_t t0 = ggml_time_us();

        // the actual inference happens here
        for (int i = 0; i < n_iter; ++i) {
            ggml_mtl_graph_compute(ctx_mtl, &gf);
        }

        const int64_t t1 = ggml_time_us();

        printf("time: %.2f ms, %.2f ms/tok\n", (t1 - t0) / 1000.0, (t1 - t0) / 1000.0 / n_iter);
    }

    // debug output
    {
        struct ggml_tensor * logits = gf.nodes[gf.n_nodes - 1];
        ggml_mtl_get_tensor(ctx_mtl, logits);

        float * ptr = (float *) ggml_get_data(logits);

        printf("logits: ");
        for (int i = 0; i < 10; i++) {
            printf("%8.4f ", ptr[i]);
        }
        printf("\n");
        int imax = 0;
        double sum = 0.0;
        double vmax = -1e9;
        for (int i = 0; i < 32000; i++) {
            sum += (double) ptr[i];
            if (ptr[i] > vmax) {
                vmax = ptr[i];
                imax = i;
            }
        }
        printf("sum: %f, imax = %d, vmax = %f\n", sum, imax, vmax);
    }

    ggml_mtl_free(ctx_mtl);

    ggml_free(ctx_data);
    ggml_free(ctx_eval);

    return 0;
}

