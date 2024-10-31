/*
    Copyright 2024 Google LLC

    Use of this source code is governed by an MIT-style
    license that can be found in the LICENSE file or at
    https://opensource.org/licenses/MIT.
*/
// SPDX-License-Identifier: MIT
#pragma once

#include "minja.hpp"
#include <json.hpp>
#include <string>
#include <vector>

using json = nlohmann::ordered_json;

namespace minja {

class chat_template {
  public:

  private:
    bool _supports_tools = true;
    // Meta-Llama-3.1-8B-Instruct's template expects arguments to be an object.
    // Most other templates (and OpenAI's API) expect the arguments object to be stringified.
    bool _requires_object_arguments = false;
    bool _supports_system_role = true;
    bool _supports_parallel_tool_calls = false;
    std::string _source;
    std::string _bos_token;
    std::string _eos_token;
    std::shared_ptr<minja::TemplateNode> _template_root;

    bool renders_needles(
        const std::vector<std::string> & needles,
        const nlohmann::ordered_json & messages,
        const nlohmann::ordered_json & tools,
        bool add_generation_prompt,
        const nlohmann::ordered_json & extra_context = nlohmann::ordered_json()) const
    {
        try {
            auto prompt = apply(messages, tools, add_generation_prompt, extra_context);
            for (const auto & needle : needles) {
                if (prompt.find(needle) == std::string::npos) {
                    return false;
                }
            }
            return true;
        } catch (const std::exception & e) {
            return false;
        }
    }

  public:
    chat_template(const std::string & source, const std::string & bos_token, const std::string & eos_token)
        : _source(source), _bos_token(bos_token), _eos_token(eos_token)
    {
        _template_root = minja::Parser::parse(_source, {
            /* .trim_blocks = */ true,
            /* .lstrip_blocks = */ true,
            /* .keep_trailing_newline = */ false,
        });
        _supports_tools = source.find("tools") != std::string::npos;
        _requires_object_arguments =
            source.find("tool_call.arguments | items") != std::string::npos
            || source.find("tool_call.arguments | tojson") != std::string::npos;
        _supports_parallel_tool_calls = source.find("tool_call_id") != std::string::npos;

        _supports_system_role = renders_needles({"<System Needle>"}, {
            {{"role", "system"}, {"content", "<System Needle>"}},
            {{"role", "user"},   {"content", "Hey"}}
        }, {}, false);
    }

    const std::string & source() const { return _source; }
    bool supports_tools() const { return _supports_tools; }
    bool supports_parallel_tool_calls() const { return _supports_parallel_tool_calls; }

    std::string apply(
        const nlohmann::ordered_json & messages,
        const nlohmann::ordered_json & tools,
        bool add_generation_prompt,
        const nlohmann::ordered_json & extra_context = nlohmann::ordered_json()) const
    {
        json actual_messages;

        // First, "fix" messages so they have a chance to be rendered correctly by the template

        if (_requires_object_arguments || !_supports_system_role || !_supports_tools) {
            actual_messages = json::array();

            std::string pending_system;
            auto flush_sys = [&]() {
                if (!pending_system.empty()) {
                    actual_messages.push_back({
                        {"role", "user"},
                        {"content", pending_system},
                    });
                    pending_system.clear();
                }
            };
            for (const auto & message_ : messages) {
                auto message = message_;
                if (!message.contains("role") || !message.contains("content")) {
                    throw std::runtime_error("message must have 'role' and 'content' fields: " + message.dump());
                }
                std::string role = message.at("role");

                if (message.contains("tool_calls")) {
                    if (_requires_object_arguments || !_supports_tools) {
                        for (auto & tool_call : message.at("tool_calls")) {
                            if (tool_call["type"] == "function") {
                                auto & function = tool_call.at("function");
                                std::string arguments = function.at("arguments");
                                function["arguments"] = json::parse(arguments);
                            }
                        }
                    }
                    if (!_supports_tools) {
                        auto content = message.at("content");
                        auto tool_calls = json::array();
                        for (const auto & tool_call : message.at("tool_calls")) {
                            if (tool_call.at("type") != "function") {
                                continue;
                            }
                            const auto & function = tool_call.at("function");
                            auto tc = json {
                                {"name", function.at("name")},
                                {"arguments", function.at("arguments")},
                            };
                            if (tool_call.contains("id")) {
                                tc["id"] = tool_call["id"];
                            }
                            tool_calls.push_back(tc);
                        }
                        auto obj = json {
                            {"tool_calls", tool_calls},
                        };
                        if (!content.is_null() && content != "") {
                            obj["content"] = content;
                        }
                        message["content"] = obj.dump(2);
                        message.erase("tool_calls");
                    }
                }
                if (!_supports_tools && role == "tool") {
                    message["role"] = "user";
                    auto obj = json {
                        {"tool_response", {
                            {"tool", message.at("name")},
                            {"content", message.at("content")},
                        }},
                    };
                    if (message.contains("tool_call_id")) {
                        obj["tool_response"]["tool_call_id"] = message.at("tool_call_id");
                    }
                    message["content"] = obj.dump(2);
                    message.erase("name");
                }

                // std::string content = message["content"];
                if (!message["content"].is_null() && !_supports_system_role) {
                    std::string content = message.at("content");
                    if (role == "system") {
                        if (!pending_system.empty()) pending_system += "\n";
                        pending_system += content;
                        continue;
                    } else {
                        if (role == "user") {
                            if (!pending_system.empty()) {
                                message["content"] = pending_system + (content.empty() ? "" : "\n" + content);
                                pending_system.clear();
                            }
                        } else {
                            flush_sys();
                        }
                    }
                }
                actual_messages.push_back(message);
            }
            flush_sys();
        } else {
            actual_messages = messages;
        }

        auto context = minja::Context::make(json({
            {"messages", actual_messages},
            {"add_generation_prompt", add_generation_prompt},
            {"bos_token", _bos_token},
            {"eos_token", _eos_token},
        }));

        if (!tools.is_null()) {
            auto tools_val = minja::Value(tools);
            context->set("tools", tools_val);
        }
        if (!extra_context.is_null()) {
            for (auto & kv : extra_context.items()) {
                minja::Value val(kv.value());
                context->set(kv.key(), val);
            }
        }

        return _template_root->render(context);
    }
};

}  // namespace minja
