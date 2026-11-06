sequenceDiagram
    autonumber
    actor User as run_eval (Main)
    participant HR as HarnessRunner
    participant ENV as Environment (Sandbox/Native)
    participant AL as AgentLoop
    participant TRAJ as Trajectory
    participant EVAL as Evaluator (Functional/Keyword/VLM)

    User->>HR: runBatchEvaluation("benchmark/tasks.json", "trajectories")
    HR->>HR: loadTasksFromJson("benchmark/tasks.json")

    loop For each TaskSpec in tasks
        HR->>HR: runSingleTask(task, output_dir)
        HR->>TRAJ: Create Trajectory(task.id, model_name)
        HR->>ENV: setup() (Create/Switch to workspace)
        ENV-->>HR: true

        HR->>AL: setMaxSteps(task.max_steps)
        HR->>AL: setStepHook(lambda [&traj]) (Observer Pattern)
        HR->>AL: run(task.instruction)

        loop Each Step in AgentLoop
            AL-->>HR: Invoke step_hook(StepEvent)
            HR->>TRAJ: addStep(Step)
        end

        AL-->>HR: Return AgentRunResult
        HR->>EVAL: evaluate(task, run_result.final_answer) (Strategy Pattern)
        EVAL-->>HR: Return EvalResult (passed, partial_score, reason)

        HR->>TRAJ: Update success, eval_score, eval_reason
        HR->>TRAJ: saveToFile("trajectories/trajectory_{id}.json")
        HR->>AL: setStepHook(nullptr) (RAII ScopeGuard)
        HR->>ENV: teardown() (Restore working directory)
    end

    HR->>HR: Calculate success_rate & write "benchmark_summary.json"
    HR-->>User: Return BatchEvalSummary