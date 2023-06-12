#!/bin/bash

API_URL="http://127.0.0.1:8080"

CHAT=(
    "Hello, Assistant."
    "Hello. How may I help you today?"
    "Please tell me the largest city in Europe."
    "Sure. The largest city in Europe is Moscow, the capital of Russia."
)

INSTRUCTION="A chat between a curious human and an artificial intelligence assistant. The assistant gives helpful, detailed, and polite answers to the human's questions."

format_prompt() {
    echo -n "${INSTRUCTION}"
    printf "\n### Human: %s\n### Assistant: %s" "${CHAT[@]}" "$1"
}

tokenize() {
    curl \
        --silent \
        --request POST \
        --url "${API_URL}/tokenize" \
        --data-raw "$(jq -ns --arg content "$1" '{content:$content}')" \
    | jq '.tokens[]'
}

N_KEEP=$(tokenize "${INSTRUCTION}" | wc -l)

chat_completion() {
    DATA="$(format_prompt "$1" | jq -Rs --argjson n_keep $N_KEEP '{
        prompt: .,
        temperature: 0.2,
        top_k: 40,
        top_p: 0.9,
        n_keep: $n_keep,
        n_predict: 256,
        stop: ["\n### Human:"],
        stream: true
    }')"

    ANSWER=''

    while IFS= read -r LINE; do
        if [[ $LINE = data:* ]]; then
            CONTENT="$(echo "${LINE:5}" | jq -r '.content')"
            printf "%s" "${CONTENT}"
            ANSWER+="${CONTENT}"
        fi
    done < <(curl \
        --silent \
        --no-buffer \
        --request POST \
        --url "${API_URL}/completion" \
        --data-raw "${DATA}")

    printf "\n"

    CHAT+=("$1" "${ANSWER:1}")
}


while true; do
    read -e -p "> " QUESTION
    chat_completion "${QUESTION}"
done
