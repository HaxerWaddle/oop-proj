sequenceDiagram
    autonumber
    actor Caller as Caller / Harness
    participant AL as AgentLoop
    participant SL as SkillLoader
    participant TR as ToolRegistry
    participant LLM as LLMClient (OllamaClient)
    participant LD as LoopDetector
    participant T as Concrete Tool

    Caller->>AL: run(task_instruction, images_base64)
    AL->>LD: reset()
    AL->>SL: buildSkillPromptSection(task_instruction)
    SL-->>AL: Matched Markdown Skills
    AL->>TR: generateToolsPrompt()
    TR-->>AL: Allowed Tools Descriptions
    AL->>AL: Construct System + User Message History

    loop ReAct Loop (step = 0 .. max_steps - 1)
        AL->>LLM: think(conversation_history) -> chat()
        LLM-->>AL: expected<LLMResponse, LLMError>
        AL->>AL: parseLLMOutput(response.content)

        alt Action is DoneAction
            AL-->>Caller: Emit step_hook(StepEvent[done])
            AL-->>Caller: Return AgentRunResult (completed = true)
        else Action is ToolCallAction / GUI Action
            AL->>LD: recordAction(tool_name, tool_args)
            LD-->>AL: LoopCheckResult (None / Warning / Critical)

            alt LoopStatus is CriticalRepeat or CriticalPingPong
                AL-->>Caller: Emit step_hook(StepEvent[loop_stopped])
                AL-->>Caller: Return AgentRunResult (stopped_by_loop = true)
            else LoopStatus is None or Warning
                AL->>TR: act(action) -> executeTool(tool_name, args)
                TR->>TR: isAllowed(tool_name) (Check Policy)
                TR->>T: execute(args_json)
                T-->>TR: optional<ToolResult>
                TR-->>AL: optional<ToolResult>
                AL-->>Caller: Emit step_hook(StepEvent[tool_call])
                AL->>AL: observe(tool_result, step)
                AL->>AL: Append Observation (+ Warning if any) to History
            end
        end
    end

    AL-->>Caller: Return AgentRunResult (reached_max_steps = true)