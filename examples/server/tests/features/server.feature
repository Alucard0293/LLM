@llama.cpp
@server
Feature: llama.cpp server

  Background: Server startup
    Given a server listening on localhost:8080
    And   a model url https://huggingface.co/ggml-org/models/resolve/main/tinyllamas/stories260K.gguf
    And   a model file stories260K.gguf
    And   a model alias tinyllama-2
    And   42 as server seed
      # KV Cache corresponds to the total amount of tokens
      # that can be stored across all independent sequences: #4130
      # see --ctx-size and #5568
    And   256 KV cache size
    And   32 as batch size
    And   2 slots
    And   64 server max tokens to predict
    And   prometheus compatible metrics exposed
    Then  the server is starting
    Then  the server is healthy

  Scenario: Health
    Then the server is ready
    And  all slots are idle


  Scenario Outline: Completion
    Given a prompt <prompt>
    And   <n_predict> max tokens to predict
    And   a completion request with no api error
    Then  <n_predicted> tokens are predicted matching <re_content>
    And   the completion is <truncated> truncated
    And   <n_prompt> prompt tokens are processed
    And   prometheus metrics are exposed
    And   metric llamacpp:tokens_predicted is <n_predicted>

    Examples: Prompts
      | prompt                                                                    | n_predict | re_content                                  | n_prompt | n_predicted | truncated |
      | I believe the meaning of life is                                          | 8         | (read\|going)+                              | 18       | 8           | not       |
      | Write a joke about AI from a very long prompt which will not be truncated | 256       | (princesses\|everyone\|kids\|Anna\|forest)+ | 46       | 64          | not       |

  Scenario: Completion prompt truncated
    Given a prompt:
    """
    Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.
    Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.
    Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur.
    Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.
    """
    And   a completion request with no api error
    Then  64 tokens are predicted matching fun|Annaks|popcorns|pictry
    And   the completion is  truncated
    And   109 prompt tokens are processed


  Scenario Outline: OAI Compatibility
    Given a model <model>
    And   a system prompt <system_prompt>
    And   a user prompt <user_prompt>
    And   <max_tokens> max tokens to predict
    And   streaming is <enable_streaming>
    Given an OAI compatible chat completions request with no api error
    Then  <n_predicted> tokens are predicted matching <re_content>
    And   <n_prompt> prompt tokens are processed
    And   the completion is <truncated> truncated

    Examples: Prompts
      | model        | system_prompt               | user_prompt                          | max_tokens | re_content                        | n_prompt | n_predicted | enable_streaming | truncated |
      | llama-2      | Book                        | What is the best book                | 8          | (Here\|what)+                     | 77       | 8           | disabled         | not       |
      | codellama70b | You are a coding assistant. | Write the fibonacci function in c++. | 128        | (thanks\|happy\|bird\|Annabyear)+ | -1       | 64          | enabled          |           |


  Scenario: Tokenize / Detokenize
    When tokenizing:
    """
    What is the capital of France ?
    """
    Then tokens can be detokenize

  Scenario: Models available
    Given available models
    Then  1 models are supported
    Then  model 0 is identified by tinyllama-2
    Then  model 0 is trained on 128 tokens context
