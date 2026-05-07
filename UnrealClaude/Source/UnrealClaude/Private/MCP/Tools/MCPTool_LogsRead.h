// Copyright Ban Ming. All Rights Reserved.
// Portions adapted from VibeUE (MIT) (c) 2025 Kevin Buckley / Buckley Builds LLC.
// https://github.com/buckleybuilds/VibeUE

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: read operations against on-disk UE log files in Saved/Logs/.
 *
 * Unlike get_output_log (which reads the in-memory output log ring buffer),
 * this tool reads the persistent *.log files written by the engine.
 * These files survive editor restarts and contain the full session history.
 *
 * All operations accept an optional `file_name` parameter.
 * When omitted the tool targets the current project log:
 *   <ProjectLogDir>/<ProjectName>.log
 *
 * Operations:
 *   list     - List all *.log files in ProjectLogDir with metadata.
 *   info     - Return path, size, modification time, and line count for one file.
 *   read     - Return a paginated slice of lines (offset + limit).
 *   tail     - Return the last N lines.
 *   head     - Return the first N lines.
 *   filter   - Return lines matching a regex with line numbers.
 *   errors   - Return lines that contain UE error tokens (": Error:" / "Error: ").
 *   warnings - Return lines that contain UE warning tokens.
 *   since    - Return all lines from start_line onward (poll-friendly).
 *
 * Security: file_name values containing ".." or resolving outside
 * ProjectLogDir are rejected to prevent path traversal.
 */
class FMCPTool_LogsRead : public FMCPToolBase
{
public:
    /**
     * Return tool metadata: name, description, parameter schema, annotations.
     * Called by the registry to expose this tool to MCP clients.
     */
    virtual FMCPToolInfo GetInfo() const override;

    /**
     * Dispatch the requested operation to the appropriate private handler.
     *
     * @param Params - JSON object containing at minimum "operation".
     * @return FMCPToolResult with structured JSON data on success, or an error
     *         message when the operation or file is invalid.
     */
    virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
    // -------------------------------------------------------------------------
    // Path helpers
    // -------------------------------------------------------------------------

    /**
     * Resolve the optional file_name parameter to an absolute path.
     *
     * Resolution rules (in order):
     *   1. If file_name is empty — use <ProjectLogDir>/<ProjectName>.log.
     *   2. If file_name contains a path separator — treat as already qualified;
     *      normalize with FPaths::ConvertRelativePathToFull.
     *   3. Otherwise — join with FPaths::ProjectLogDir().
     *
     * After resolution the path is validated:
     *   - Must be inside FPaths::ProjectLogDir() (anti-traversal).
     *   - Must reference an existing file.
     *
     * @param Params   - Incoming JSON params (may contain "file_name").
     * @param OutPath  - Resolved absolute path on success.
     * @param OutError - Populated with an error result on failure.
     * @return true when resolution and validation both succeed.
     */
    bool ResolveLogPath(const TSharedRef<FJsonObject>& Params,
                        FString& OutPath,
                        TOptional<FMCPToolResult>& OutError) const;

    /**
     * Load all lines from the given absolute path into OutLines.
     * Uses FILEREAD_AllowWrite so the active engine log (held open by UE)
     * can be read without conflict.
     *
     * @param FullPath  - Absolute path to the log file.
     * @param OutLines  - Receives all lines (CR stripped).
     * @param OutError  - Human-readable error string on failure.
     * @return true on success.
     */
    static bool LoadLines(const FString& FullPath,
                          TArray<FString>& OutLines,
                          FString& OutError);

    // -------------------------------------------------------------------------
    // Operation handlers — each returns a fully-formed FMCPToolResult
    // -------------------------------------------------------------------------

    /**
     * list: enumerate all *.log files in ProjectLogDir.
     * Output: { files: [{name, size_bytes, modified_iso}] }
     */
    FMCPToolResult ExecuteList(const TSharedRef<FJsonObject>& Params);

    /**
     * info: file metadata + line count for one log file.
     * Output: { path, size_bytes, modified_iso, line_count }
     */
    FMCPToolResult ExecuteInfo(const TSharedRef<FJsonObject>& Params);

    /**
     * read: paginated line retrieval.
     * Inputs: file_name?, offset?:0, limit?:500
     * Output: { lines, total_lines, offset, limit, has_more }
     */
    FMCPToolResult ExecuteRead(const TSharedRef<FJsonObject>& Params);

    /**
     * tail: last N lines of the file.
     * Inputs: file_name?, count?:100
     * Output: { lines, from_line, total_lines }
     */
    FMCPToolResult ExecuteTail(const TSharedRef<FJsonObject>& Params);

    /**
     * head: first N lines of the file.
     * Inputs: file_name?, count?:100
     * Output: { lines, total_lines }
     */
    FMCPToolResult ExecuteHead(const TSharedRef<FJsonObject>& Params);

    /**
     * filter: return lines matching a regex with their 1-based line numbers.
     * Inputs: file_name?, regex (required), max_matches?:200
     * Output: { matches: [{line_number, content}], match_count, max_reached }
     *
     * Note: UE's FRegexPattern does not throw on invalid patterns; a bad regex
     * will simply match nothing. The description notes this limitation.
     */
    FMCPToolResult ExecuteFilter(const TSharedRef<FJsonObject>& Params);

    /**
     * errors: lines containing UE error tokens (": Error:" or "Error: ").
     * Inputs: file_name?, count?:50
     * Output: { errors: [{line_number, content, category?}], error_count }
     */
    FMCPToolResult ExecuteErrors(const TSharedRef<FJsonObject>& Params);

    /**
     * warnings: lines containing UE warning tokens (": Warning:" or "Warning: ").
     * Inputs: file_name?, count?:50
     * Output: { warnings: [{line_number, content, category?}], warning_count }
     */
    FMCPToolResult ExecuteWarnings(const TSharedRef<FJsonObject>& Params);

    /**
     * since: all lines from start_line (1-based) onward. Useful for polling.
     * Inputs: file_name?, start_line (required)
     * Output: { lines, next_start_line, total_lines }
     */
    FMCPToolResult ExecuteSince(const TSharedRef<FJsonObject>& Params);

    // -------------------------------------------------------------------------
    // Shared helpers
    // -------------------------------------------------------------------------

    /**
     * Extract the UE log category from a standard log line.
     * Standard format: [timestamp][frame]LogCategory: Verbosity: Message
     * Returns empty string when the format is not recognized.
     *
     * @param Line - A single log line.
     * @return The extracted category string, or empty if not found.
     */
    static FString ExtractCategory(const FString& Line);

    /**
     * Convert a TArray<FString> slice to a JSON array of string values.
     *
     * @param Lines - Source lines.
     * @return JSON array value array suitable for SetArrayField.
     */
    static TArray<TSharedPtr<FJsonValue>> LinesToJsonArray(const TArray<FString>& Lines);

    /**
     * Build a JSON object for a single match entry used by filter/errors/warnings.
     *
     * @param LineNumber  - 1-based line number.
     * @param Content     - Line content.
     * @param bAddCategory - When true, attempt to extract and add "category" field.
     * @return Shared JSON object { line_number, content[, category] }.
     */
    static TSharedPtr<FJsonObject> BuildMatchEntry(int32 LineNumber,
                                                    const FString& Content,
                                                    bool bAddCategory);
};
