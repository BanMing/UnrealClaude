// Copyright Ban Ming. All Rights Reserved.
// Portions adapted from VibeUE (MIT) (c) 2025 Kevin Buckley / Buckley Builds LLC.
// https://github.com/buckleybuilds/VibeUE

#include "MCPTool_LogsRead.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"

// ============================================================================
// GetInfo
// ============================================================================

FMCPToolInfo FMCPTool_LogsRead::GetInfo() const
{
    FMCPToolInfo Info;
    Info.Name = TEXT("logs_read");
    Info.Description = TEXT(
        "Read on-disk UE log files from Saved/Logs/*.log — persistent across sessions.\n\n"
        "This tool reads file-based logs written to disk by the engine (Saved/Logs/). "
        "It complements get_output_log (which reads the in-memory ring buffer that "
        "resets on editor restart).\n\n"
        "All operations accept an optional file_name. When omitted, the current "
        "project log (<ProjectName>.log) is used.\n\n"
        "Operations:\n"
        "- list:     List all *.log files in Saved/Logs/ with size and modified time.\n"
        "- info:     Metadata + line count for a single file.\n"
        "- read:     Paginated line retrieval. Inputs: offset (default 0), limit (default 500).\n"
        "- tail:     Last N lines. Inputs: count (default 100).\n"
        "- head:     First N lines. Inputs: count (default 100).\n"
        "- filter:   Lines matching a regex. Inputs: regex (required), max_matches (default 200).\n"
        "            NOTE: an invalid regex silently matches nothing (UE FRegexPattern behavior).\n"
        "- errors:   Lines containing UE error tokens. Inputs: count (default 50).\n"
        "- warnings: Lines containing UE warning tokens. Inputs: count (default 50).\n"
        "- since:    All lines from start_line (1-based) onward (poll-friendly). "
        "            Inputs: start_line (required).");

    Info.Parameters = {
        FMCPToolParameter(TEXT("operation"), TEXT("string"),
            TEXT("Operation: list | info | read | tail | head | filter | errors | warnings | since"),
            true),
        FMCPToolParameter(TEXT("file_name"), TEXT("string"),
            TEXT("Log file name (bare name like 'Paoge.log') or full path. "
                 "Defaults to <ProjectName>.log when omitted. "
                 "Must reside inside Saved/Logs/ — path traversal is rejected."),
            false),
        // read
        FMCPToolParameter(TEXT("offset"), TEXT("number"),
            TEXT("read: 0-based line offset to start reading from. Default: 0."),
            false, TEXT("0")),
        FMCPToolParameter(TEXT("limit"), TEXT("number"),
            TEXT("read: maximum number of lines to return. Default: 500."),
            false, TEXT("500")),
        // tail / head
        FMCPToolParameter(TEXT("count"), TEXT("number"),
            TEXT("tail/head/errors/warnings: number of lines to return. Default: 100 (tail/head) or 50 (errors/warnings)."),
            false),
        // filter
        FMCPToolParameter(TEXT("regex"), TEXT("string"),
            TEXT("filter: regular expression to match against each line. Required for 'filter' operation."),
            false),
        FMCPToolParameter(TEXT("max_matches"), TEXT("number"),
            TEXT("filter: maximum number of matching lines to return. Default: 200."),
            false, TEXT("200")),
        // since
        FMCPToolParameter(TEXT("start_line"), TEXT("number"),
            TEXT("since: 1-based line number to start from. Returns all lines >= start_line."),
            false),
    };

    Info.Annotations = FMCPToolAnnotations::ReadOnly();
    return Info;
}

// ============================================================================
// Execute — dispatch
// ============================================================================

FMCPToolResult FMCPTool_LogsRead::Execute(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: extract the required operation parameter.
    FString Operation;
    TOptional<FMCPToolResult> OpError;
    if (!ExtractRequiredString(Params, TEXT("operation"), Operation, OpError))
    {
        return OpError.GetValue();
    }

    // Step 2: dispatch to the appropriate handler.
    if (Operation.Equals(TEXT("list"),     ESearchCase::IgnoreCase)) return ExecuteList(Params);
    if (Operation.Equals(TEXT("info"),     ESearchCase::IgnoreCase)) return ExecuteInfo(Params);
    if (Operation.Equals(TEXT("read"),     ESearchCase::IgnoreCase)) return ExecuteRead(Params);
    if (Operation.Equals(TEXT("tail"),     ESearchCase::IgnoreCase)) return ExecuteTail(Params);
    if (Operation.Equals(TEXT("head"),     ESearchCase::IgnoreCase)) return ExecuteHead(Params);
    if (Operation.Equals(TEXT("filter"),   ESearchCase::IgnoreCase)) return ExecuteFilter(Params);
    if (Operation.Equals(TEXT("errors"),   ESearchCase::IgnoreCase)) return ExecuteErrors(Params);
    if (Operation.Equals(TEXT("warnings"), ESearchCase::IgnoreCase)) return ExecuteWarnings(Params);
    if (Operation.Equals(TEXT("since"),    ESearchCase::IgnoreCase)) return ExecuteSince(Params);

    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown operation '%s'. Valid: list, info, read, tail, head, filter, errors, warnings, since."),
        *Operation));
}

// ============================================================================
// Path helpers
// ============================================================================

bool FMCPTool_LogsRead::ResolveLogPath(const TSharedRef<FJsonObject>& Params,
                                        FString& OutPath,
                                        TOptional<FMCPToolResult>& OutError) const
{
    // Step 1: get the optional file_name parameter.
    FString FileName = ExtractOptionalString(Params, TEXT("file_name"));

    FString LogDir = FPaths::ProjectLogDir();
    // Ensure the log dir path has a trailing slash for prefix checks.
    FPaths::NormalizeDirectoryName(LogDir);

    FString FullPath;

    if (FileName.IsEmpty())
    {
        // Default: <ProjectName>.log in the project log directory.
        FullPath = FPaths::Combine(LogDir, FString(FApp::GetProjectName()) + TEXT(".log"));
    }
    else
    {
        // Step 2: reject obvious traversal attempts before doing anything else.
        if (FileName.Contains(TEXT("..")))
        {
            OutError = FMCPToolResult::Error(
                TEXT("file_name must not contain '..'. Path traversal is not allowed."));
            return false;
        }

        bool bHasSeparator = FileName.Contains(TEXT("/")) || FileName.Contains(TEXT("\\"));
        if (bHasSeparator)
        {
            // Treat as an already-qualified path; normalize it.
            FullPath = FPaths::ConvertRelativePathToFull(FileName);
        }
        else
        {
            // Bare filename — resolve relative to ProjectLogDir.
            FullPath = FPaths::Combine(LogDir, FileName);
        }
    }

    // Step 3: canonicalize the path (resolve any remaining . components).
    FPaths::NormalizeFilename(FullPath);

    // Step 4: enforce containment inside ProjectLogDir (anti-traversal).
    if (!FPaths::IsUnderDirectory(FullPath, LogDir))
    {
        OutError = FMCPToolResult::Error(FString::Printf(
            TEXT("Resolved path '%s' is outside Saved/Logs/. Access denied."), *FullPath));
        return false;
    }

    // Step 5: verify the file exists (skip for 'list' — list never calls this).
    if (!IFileManager::Get().FileExists(*FullPath))
    {
        OutError = FMCPToolResult::Error(FString::Printf(
            TEXT("Log file not found: %s"), *FullPath));
        return false;
    }

    OutPath = FullPath;
    return true;
}

bool FMCPTool_LogsRead::LoadLines(const FString& FullPath,
                                   TArray<FString>& OutLines,
                                   FString& OutError)
{
    // Use FILEREAD_AllowWrite so we can read the active engine log file
    // that UE holds open for writing throughout the session.
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*FullPath, FILEREAD_AllowWrite));
    if (!Reader)
    {
        OutError = FString::Printf(TEXT("Failed to open log file: %s"), *FullPath);
        return false;
    }

    // Step 1: read raw bytes.
    int64 TotalSize = Reader->TotalSize();
    TArray<uint8> RawBytes;
    if (TotalSize > 0)
    {
        RawBytes.SetNumUninitialized(static_cast<int32>(TotalSize));
        Reader->Serialize(RawBytes.GetData(), TotalSize);
    }
    Reader->Close();

    // Step 2: convert bytes to FString (UTF-8 assumed, same as VibeUE reference).
    FString Content;
    FFileHelper::BufferToString(Content, RawBytes.GetData(), RawBytes.Num());

    // Step 3: split on newline, keeping empty lines so line numbers stay accurate.
    Content.ParseIntoArray(OutLines, TEXT("\n"), false);

    // Step 4: strip trailing \r from each line (Windows CRLF line endings).
    for (FString& Line : OutLines)
    {
        if (Line.EndsWith(TEXT("\r")))
        {
            Line.RemoveAt(Line.Len() - 1);
        }
    }

    return true;
}

// ============================================================================
// Operation: list
// ============================================================================

FMCPToolResult FMCPTool_LogsRead::ExecuteList(const TSharedRef<FJsonObject>& Params)
{
    FString LogDir = FPaths::ProjectLogDir();

    // Find all *.log files (non-recursive — keep it to the standard log dir).
    TArray<FString> FoundPaths;
    IFileManager::Get().FindFiles(FoundPaths, *(LogDir / TEXT("*.log")), true, false);

    // Build JSON array sorted by name for deterministic output.
    FoundPaths.Sort();

    TArray<TSharedPtr<FJsonValue>> FilesArray;
    for (const FString& RelName : FoundPaths)
    {
        FString FullPath = FPaths::Combine(LogDir, RelName);

        // Gather metadata.
        int64   SizeBytes    = IFileManager::Get().FileSize(*FullPath);
        FDateTime ModifiedTime = IFileManager::Get().GetTimeStamp(*FullPath);

        TSharedPtr<FJsonObject> FileObj = MakeShared<FJsonObject>();
        FileObj->SetStringField(TEXT("name"),          RelName);
        FileObj->SetNumberField(TEXT("size_bytes"),    static_cast<double>(SizeBytes));
        FileObj->SetStringField(TEXT("modified_iso"),  ModifiedTime.ToIso8601());

        FilesArray.Add(MakeShared<FJsonValueObject>(FileObj));
    }

    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("files"), FilesArray);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Found %d log file(s) in Saved/Logs/"), FilesArray.Num()),
        ResultData);
}

// ============================================================================
// Operation: info
// ============================================================================

FMCPToolResult FMCPTool_LogsRead::ExecuteInfo(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: resolve the target file path.
    FString FullPath;
    TOptional<FMCPToolResult> PathError;
    if (!ResolveLogPath(Params, FullPath, PathError))
    {
        return PathError.GetValue();
    }

    // Step 2: gather metadata.
    int64     SizeBytes    = IFileManager::Get().FileSize(*FullPath);
    FDateTime ModifiedTime = IFileManager::Get().GetTimeStamp(*FullPath);

    // Step 3: count lines by loading (simple v1 approach; acceptable for typical log sizes).
    TArray<FString> Lines;
    FString LoadError;
    int32 LineCount = 0;
    if (LoadLines(FullPath, Lines, LoadError))
    {
        LineCount = Lines.Num();
    }

    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetStringField(TEXT("path"),          FullPath);
    ResultData->SetNumberField(TEXT("size_bytes"),    static_cast<double>(SizeBytes));
    ResultData->SetStringField(TEXT("modified_iso"),  ModifiedTime.ToIso8601());
    ResultData->SetNumberField(TEXT("line_count"),    LineCount);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Log info: %s (%lld bytes, %d lines)"),
            *FPaths::GetCleanFilename(FullPath), SizeBytes, LineCount),
        ResultData);
}

// ============================================================================
// Operation: read
// ============================================================================

FMCPToolResult FMCPTool_LogsRead::ExecuteRead(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: resolve file.
    FString FullPath;
    TOptional<FMCPToolResult> PathError;
    if (!ResolveLogPath(Params, FullPath, PathError))
    {
        return PathError.GetValue();
    }

    // Step 2: extract pagination params.
    int32 Offset = FMath::Max(0, ExtractOptionalNumber<int32>(Params, TEXT("offset"), 0));
    int32 Limit  = FMath::Clamp(ExtractOptionalNumber<int32>(Params, TEXT("limit"),  500), 1, 10000);

    // Step 3: load all lines.
    TArray<FString> AllLines;
    FString LoadError;
    if (!LoadLines(FullPath, AllLines, LoadError))
    {
        return FMCPToolResult::Error(LoadError);
    }

    int32 TotalLines  = AllLines.Num();
    int32 StartIndex  = FMath::Min(Offset, TotalLines);
    int32 EndIndex    = FMath::Min(StartIndex + Limit, TotalLines);
    bool  bHasMore    = EndIndex < TotalLines;

    // Step 4: extract the requested slice.
    TArray<FString> Slice;
    for (int32 i = StartIndex; i < EndIndex; ++i)
    {
        Slice.Add(AllLines[i]);
    }

    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("lines"),       LinesToJsonArray(Slice));
    ResultData->SetNumberField(TEXT("total_lines"), TotalLines);
    ResultData->SetNumberField(TEXT("offset"),      StartIndex);
    ResultData->SetNumberField(TEXT("limit"),       Limit);
    ResultData->SetBoolField  (TEXT("has_more"),    bHasMore);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Read %d lines (offset %d, total %d)"),
            Slice.Num(), StartIndex, TotalLines),
        ResultData);
}

// ============================================================================
// Operation: tail
// ============================================================================

FMCPToolResult FMCPTool_LogsRead::ExecuteTail(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: resolve file.
    FString FullPath;
    TOptional<FMCPToolResult> PathError;
    if (!ResolveLogPath(Params, FullPath, PathError))
    {
        return PathError.GetValue();
    }

    // Step 2: extract count param (default 100 for tail).
    int32 Count = FMath::Max(1, ExtractOptionalNumber<int32>(Params, TEXT("count"), 100));

    // Step 3: load all lines.
    TArray<FString> AllLines;
    FString LoadError;
    if (!LoadLines(FullPath, AllLines, LoadError))
    {
        return FMCPToolResult::Error(LoadError);
    }

    int32 TotalLines = AllLines.Num();
    // from_line is 1-based so clients know where in the file these lines came from.
    int32 StartIndex = FMath::Max(0, TotalLines - Count);
    int32 FromLine   = StartIndex + 1;  // convert to 1-based

    TArray<FString> Slice;
    for (int32 i = StartIndex; i < TotalLines; ++i)
    {
        Slice.Add(AllLines[i]);
    }

    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("lines"),       LinesToJsonArray(Slice));
    ResultData->SetNumberField(TEXT("from_line"),   FromLine);
    ResultData->SetNumberField(TEXT("total_lines"), TotalLines);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Tail %d lines from line %d (total %d)"),
            Slice.Num(), FromLine, TotalLines),
        ResultData);
}

// ============================================================================
// Operation: head
// ============================================================================

FMCPToolResult FMCPTool_LogsRead::ExecuteHead(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: resolve file.
    FString FullPath;
    TOptional<FMCPToolResult> PathError;
    if (!ResolveLogPath(Params, FullPath, PathError))
    {
        return PathError.GetValue();
    }

    // Step 2: extract count param (default 100 for head).
    int32 Count = FMath::Max(1, ExtractOptionalNumber<int32>(Params, TEXT("count"), 100));

    // Step 3: load all lines.
    TArray<FString> AllLines;
    FString LoadError;
    if (!LoadLines(FullPath, AllLines, LoadError))
    {
        return FMCPToolResult::Error(LoadError);
    }

    int32 TotalLines = AllLines.Num();
    int32 EndIndex   = FMath::Min(Count, TotalLines);

    TArray<FString> Slice;
    for (int32 i = 0; i < EndIndex; ++i)
    {
        Slice.Add(AllLines[i]);
    }

    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("lines"),       LinesToJsonArray(Slice));
    ResultData->SetNumberField(TEXT("total_lines"), TotalLines);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Head %d lines (total %d)"), Slice.Num(), TotalLines),
        ResultData);
}

// ============================================================================
// Operation: filter
// ============================================================================

FMCPToolResult FMCPTool_LogsRead::ExecuteFilter(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: resolve file.
    FString FullPath;
    TOptional<FMCPToolResult> PathError;
    if (!ResolveLogPath(Params, FullPath, PathError))
    {
        return PathError.GetValue();
    }

    // Step 2: extract required regex parameter.
    FString RegexStr;
    TOptional<FMCPToolResult> RegexError;
    if (!ExtractRequiredString(Params, TEXT("regex"), RegexStr, RegexError))
    {
        return RegexError.GetValue();
    }

    int32 MaxMatches = FMath::Max(1, ExtractOptionalNumber<int32>(Params, TEXT("max_matches"), 200));

    // Step 3: load lines.
    TArray<FString> AllLines;
    FString LoadError;
    if (!LoadLines(FullPath, AllLines, LoadError))
    {
        return FMCPToolResult::Error(LoadError);
    }

    // Step 4: compile pattern and iterate.
    // Note: FRegexPattern does not throw on invalid patterns — a bad regex
    // will silently match nothing (documented in tool description).
    FRegexPattern Pattern(RegexStr, ERegexPatternFlags::CaseInsensitive);

    TArray<TSharedPtr<FJsonValue>> MatchesArray;
    bool bMaxReached = false;

    for (int32 i = 0; i < AllLines.Num(); ++i)
    {
        FRegexMatcher Matcher(Pattern, AllLines[i]);
        if (Matcher.FindNext())
        {
            MatchesArray.Add(MakeShared<FJsonValueObject>(
                BuildMatchEntry(i + 1, AllLines[i], false)));

            if (MatchesArray.Num() >= MaxMatches)
            {
                bMaxReached = true;
                break;
            }
        }
    }

    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("matches"),     MatchesArray);
    ResultData->SetNumberField(TEXT("match_count"), MatchesArray.Num());
    ResultData->SetBoolField  (TEXT("max_reached"), bMaxReached);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Filter found %d match(es)%s"),
            MatchesArray.Num(), bMaxReached ? TEXT(" (limit reached)") : TEXT("")),
        ResultData);
}

// ============================================================================
// Operation: errors
// ============================================================================

FMCPToolResult FMCPTool_LogsRead::ExecuteErrors(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: resolve file.
    FString FullPath;
    TOptional<FMCPToolResult> PathError;
    if (!ResolveLogPath(Params, FullPath, PathError))
    {
        return PathError.GetValue();
    }

    // Step 2: extract count param (default 50 for errors).
    int32 Count = FMath::Max(1, ExtractOptionalNumber<int32>(Params, TEXT("count"), 50));

    // Step 3: load lines.
    TArray<FString> AllLines;
    FString LoadError;
    if (!LoadLines(FullPath, AllLines, LoadError))
    {
        return FMCPToolResult::Error(LoadError);
    }

    // Step 4: match UE error verbosity field strictly.
    // Standard UE log format: [timestamp][frame]LogCategory: Error: Message
    // Anchor on `LogXxx: Error: ` so message bodies that contain the literal
    // word "error" (e.g. `libcurl error: 35` inside a Warning line) do NOT
    // match. Case-sensitive — UE verbosity is always capitalized.
    FRegexPattern Pattern(TEXT("Log\\w+: Error: "));

    TArray<TSharedPtr<FJsonValue>> ErrorsArray;

    for (int32 i = 0; i < AllLines.Num() && ErrorsArray.Num() < Count; ++i)
    {
        FRegexMatcher Matcher(Pattern, AllLines[i]);
        if (Matcher.FindNext())
        {
            ErrorsArray.Add(MakeShared<FJsonValueObject>(
                BuildMatchEntry(i + 1, AllLines[i], true)));
        }
    }

    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("errors"),      ErrorsArray);
    ResultData->SetNumberField(TEXT("error_count"), ErrorsArray.Num());

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Found %d error line(s)"), ErrorsArray.Num()),
        ResultData);
}

// ============================================================================
// Operation: warnings
// ============================================================================

FMCPToolResult FMCPTool_LogsRead::ExecuteWarnings(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: resolve file.
    FString FullPath;
    TOptional<FMCPToolResult> PathError;
    if (!ResolveLogPath(Params, FullPath, PathError))
    {
        return PathError.GetValue();
    }

    // Step 2: extract count param (default 50 for warnings).
    int32 Count = FMath::Max(1, ExtractOptionalNumber<int32>(Params, TEXT("count"), 50));

    // Step 3: load lines.
    TArray<FString> AllLines;
    FString LoadError;
    if (!LoadLines(FullPath, AllLines, LoadError))
    {
        return FMCPToolResult::Error(LoadError);
    }

    // Step 4: match UE warning verbosity field strictly.
    // Standard UE log format: [timestamp][frame]LogCategory: Warning: Message
    // Anchor on `LogXxx: Warning: ` to avoid matching message bodies that
    // contain the literal word "warning". Case-sensitive.
    FRegexPattern Pattern(TEXT("Log\\w+: Warning: "));

    TArray<TSharedPtr<FJsonValue>> WarningsArray;

    for (int32 i = 0; i < AllLines.Num() && WarningsArray.Num() < Count; ++i)
    {
        FRegexMatcher Matcher(Pattern, AllLines[i]);
        if (Matcher.FindNext())
        {
            WarningsArray.Add(MakeShared<FJsonValueObject>(
                BuildMatchEntry(i + 1, AllLines[i], true)));
        }
    }

    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("warnings"),      WarningsArray);
    ResultData->SetNumberField(TEXT("warning_count"), WarningsArray.Num());

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Found %d warning line(s)"), WarningsArray.Num()),
        ResultData);
}

// ============================================================================
// Operation: since
// ============================================================================

FMCPToolResult FMCPTool_LogsRead::ExecuteSince(const TSharedRef<FJsonObject>& Params)
{
    // Step 1: resolve file.
    FString FullPath;
    TOptional<FMCPToolResult> PathError;
    if (!ResolveLogPath(Params, FullPath, PathError))
    {
        return PathError.GetValue();
    }

    // Step 2: extract required start_line (1-based).
    double RawStartLine = 1.0;
    if (!Params->TryGetNumberField(TEXT("start_line"), RawStartLine))
    {
        return FMCPToolResult::Error(TEXT("Missing required parameter: start_line"));
    }
    int32 StartLine1Based = FMath::Max(1, static_cast<int32>(RawStartLine));

    // Step 3: load lines.
    TArray<FString> AllLines;
    FString LoadError;
    if (!LoadLines(FullPath, AllLines, LoadError))
    {
        return FMCPToolResult::Error(LoadError);
    }

    int32 TotalLines  = AllLines.Num();
    // Convert 1-based start_line to 0-based index.
    int32 StartIndex  = FMath::Min(StartLine1Based - 1, TotalLines);
    // next_start_line tells the caller what to pass next time.
    int32 NextStart   = TotalLines + 1;  // 1-based, points past the last line

    TArray<FString> Slice;
    for (int32 i = StartIndex; i < TotalLines; ++i)
    {
        Slice.Add(AllLines[i]);
    }

    TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
    ResultData->SetArrayField(TEXT("lines"),           LinesToJsonArray(Slice));
    ResultData->SetNumberField(TEXT("next_start_line"), NextStart);
    ResultData->SetNumberField(TEXT("total_lines"),     TotalLines);

    return FMCPToolResult::Success(
        FString::Printf(TEXT("Since line %d: %d new line(s) (total %d)"),
            StartLine1Based, Slice.Num(), TotalLines),
        ResultData);
}

// ============================================================================
// Shared helpers
// ============================================================================

FString FMCPTool_LogsRead::ExtractCategory(const FString& Line)
{
    // Standard UE log line format:
    //   [YYYY.MM.DD-HH.MM.SS:ms][frame]LogCategory: Verbosity: Message
    //
    // We look for the first ']' pair that closes the frame counter, then
    // take the token up to the next ':'.
    int32 BracketClose = INDEX_NONE;
    // Find the second ']' (end of [frame] marker).
    int32 FirstClose = Line.Find(TEXT("]"));
    if (FirstClose != INDEX_NONE)
    {
        BracketClose = Line.Find(TEXT("]"), ESearchCase::CaseSensitive,
                                  ESearchDir::FromStart, FirstClose + 1);
    }

    if (BracketClose == INDEX_NONE)
    {
        return TEXT("");
    }

    // The category begins right after the second ']'.
    int32 CatStart = BracketClose + 1;
    int32 ColonPos = Line.Find(TEXT(":"), ESearchCase::CaseSensitive,
                                ESearchDir::FromStart, CatStart);
    if (ColonPos == INDEX_NONE || ColonPos <= CatStart)
    {
        return TEXT("");
    }

    return Line.Mid(CatStart, ColonPos - CatStart).TrimStartAndEnd();
}

TArray<TSharedPtr<FJsonValue>> FMCPTool_LogsRead::LinesToJsonArray(const TArray<FString>& Lines)
{
    TArray<TSharedPtr<FJsonValue>> Array;
    Array.Reserve(Lines.Num());
    for (const FString& Line : Lines)
    {
        Array.Add(MakeShared<FJsonValueString>(Line));
    }
    return Array;
}

TSharedPtr<FJsonObject> FMCPTool_LogsRead::BuildMatchEntry(int32 LineNumber,
                                                             const FString& Content,
                                                             bool bAddCategory)
{
    TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
    Entry->SetNumberField(TEXT("line_number"), LineNumber);
    Entry->SetStringField(TEXT("content"),     Content);

    if (bAddCategory)
    {
        FString Category = ExtractCategory(Content);
        if (!Category.IsEmpty())
        {
            Entry->SetStringField(TEXT("category"), Category);
        }
    }

    return Entry;
}
