<!-- keywords: file, đọc, ghi, read, write, txt, json, thời gian, date, time, inspect -->
# Skill: File System & Structured Data Operations

Guidelines for handling files, JSON data, and system queries:
1. **File Reading & Writing (`file` tool)**:
   - Always specify `"action": "read"` or `"action": "write"` explicitly in the JSON arguments.
   - Do not append extra trailing commentary inside the file content when the user asks to store a specific number or token.
2. **JSON Extraction (`json_query` tool)**:
   - Use `json_query` with `{"json_str": "...", "key": "..."}` when extracting fields from structured JSON strings.
3. **Temporal & System Queries (`datetime_info` / `system_inspect`)**:
   - Use `datetime_info` whenever the task asks for current date, time, or timestamp instead of guessing.