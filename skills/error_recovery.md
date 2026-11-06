<!-- keywords: lỗi, error, fail, retry, shell, command, exec, db, memory, sqlite, search -->
# Skill: Graceful Error Recovery & Loop Avoidance

When a tool returns `Status: ERROR` or unexpected output:
1. **Do Not Repeat Blindly**: Never invoke the exact same tool with the exact same arguments more than once after an error.
2. **Inspect Context**:
   - If `file` read fails because a file is missing, use `exec` with `{"command": "ls -la"}` or `system_inspect` to check the working directory.
   - If `exec` fails with a syntax error, simplify the shell command and verify paths.
   - If `memory` search returns no entries, try a broader keyword or save the required key first.
3. **Avoid Ping-Pong Loops**: Do not alternate endlessly between two tools without making state progress.