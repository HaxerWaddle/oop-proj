<!-- keywords: tính, nhân, cộng, trừ, chia, lưu, kết quả, plan, multi, step, calculate -->
# Skill: Structured Task Decomposition & Verification

When solving multi-step tasks (such as calculating an arithmetic expression and saving the output to a file):
1. **Decompose**: Break the user request into atomic actions. Never guess mathematical results yourself—always invoke the `calculator` tool first.
2. **Execute Sequentially**: Wait for the `Observation` from the first tool before calling the next tool.
3. **Persist Accurately**: When asked to write the result to a file (e.g., `result.txt`), pass the exact output string from the `calculator` observation into the `file` tool with `{"action": "write", "path": "<filename>", "content": "<exact_value>"}`.
4. **Conclude**: Once the file write succeeds, immediately return `{"type": "done", "answer": "..."}` without repeating the same tool call.