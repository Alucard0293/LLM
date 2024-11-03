import itertools
import json
import logging
import os
from typing import Dict, List
import urllib.parse

import requests


def _extract_values(keys, obj):
    return dict((k, v) for k in keys if (v := obj.get(k)) is not None)


# Let's keep this tool aligned w/ llama_stack.providers.impls.meta_reference.agents.tools.builtin.BraveSearch
# (see https://github.com/meta-llama/llama-stack/blob/main/llama_stack/providers/impls/meta_reference/agents/tools/builtin.py)
_brave_search_result_keys_by_type = {
    'web':       ('type', 'title', 'url', 'description', 'date', 'extra_snippets'),
    'videos':    ('type', 'title', 'url', 'description', 'date'),
    'news':      ('type', 'title', 'url', 'description'),
    'infobox':   ('type', 'title', 'url', 'description', 'long_desc'),
    'locations': ('type', 'title', 'url', 'description', 'coordinates', 'postal_address', 'contact', 'rating', 'distance', 'zoom_level'),
    'faq':       ('type', 'title', 'url', 'question', 'answer'),
}


async def brave_search(*, query: str) -> List[Dict]:
    '''
    Search the Brave Search API for the specified query.

    Parameters:
        query (str): The query to search for.

    Returns:
        List[Dict]: The search results.
    '''
    logging.debug('[brave_search] Searching for %s', query)

    max_results = 10

    url = f'https://api.search.brave.com/res/v1/web/search?q={urllib.parse.quote(query)}'
    headers = {
        'Accept': 'application/json',
        'Accept-Encoding': 'gzip',
        'X-Subscription-Token': os.environ['BRAVE_SEARCH_API_KEY'],
    }

    def extract_results(search_response):
        # print("SEARCH RESPONSE: " + json.dumps(search_response, indent=2))
        for m in search_response['mixed']['main']:
            result_type = m['type']
            keys = _brave_search_result_keys_by_type.get(result_type)
            if keys is None:
                logging.warning(f'[brave_search] Unknown result type: %s', result_type)
                continue

            results_of_type = search_response[result_type]['results']
            if (idx := m.get('index')) is not None:
                yield _extract_values(keys, results_of_type[idx])
            elif m['all']:
                for r in results_of_type:
                    yield _extract_values(keys, r)

    response = requests.get(url, headers=headers)
    if not response.ok:
        raise Exception(response.text)
    response.raise_for_status()
    response_json = response.json()
    results = list(itertools.islice(extract_results(response_json), max_results))
    print(json.dumps(dict(query=query, response=response_json, results=results), indent=2))
    return results
