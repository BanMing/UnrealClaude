// Copyright Ban Ming. All Rights Reserved.
// Portions adapted from VibeUE (MIT) (c) 2025 Kevin Buckley / Buckley Builds LLC.
// https://github.com/buckleybuilds/VibeUE

#include "MCPTool_WebResearch.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "HAL/PlatformProcess.h"

// ---------------------------------------------------------------------------
// GetInfo
// ---------------------------------------------------------------------------

FMCPToolInfo FMCPTool_WebResearch::GetInfo() const
{
	FMCPToolInfo Info;
	Info.Name = TEXT("web_research");
	Info.Description = TEXT(
		"Compound web-research tool for use inside the Unreal Editor process "
		"(e.g., called from execute_script Python). All requests are synchronous "
		"blocking calls — expect up to 10 s per request.\n\n"
		"Operations (required 'operation' param):\n"
		"  search          — DuckDuckGo HTML scrape: returns [{title,url,snippet}] "
		                     "plus 'result_count'. Optional 'max_results' (default 5).\n"
		"  fetch_page      — Jina AI Reader (r.jina.ai): fetches any public URL as "
		                     "clean markdown. Response capped at ~1 MB by Jina. "
		                     "Returns {title,markdown,fetched_at,source_url}.\n"
		"  geocode         — Nominatim forward geocoding: 'place_name' → {lat,lon,display_name}.\n"
		"  reverse_geocode — Nominatim reverse geocoding: 'lat'+'lon' numbers → "
		                     "{display_name, address:{...}}.\n\n"
		"Rate-limit note: DuckDuckGo may return HTTP 202 or a rate-limit page if "
		"called too frequently. Nominatim requires a valid User-Agent header (set "
		"automatically) and prohibits more than 1 request/second."
	);
	Info.Parameters = {
		FMCPToolParameter(TEXT("operation"), TEXT("string"),
			TEXT("Sub-operation: search | fetch_page | geocode | reverse_geocode"), true),
		FMCPToolParameter(TEXT("query"), TEXT("string"),
			TEXT("Search query string (required for 'search' operation)"), false),
		FMCPToolParameter(TEXT("max_results"), TEXT("number"),
			TEXT("Maximum search results to return for 'search' (default 5, max 20)"), false, TEXT("5")),
		FMCPToolParameter(TEXT("url"), TEXT("string"),
			TEXT("Full URL to fetch as markdown (required for 'fetch_page' operation)"), false),
		FMCPToolParameter(TEXT("place_name"), TEXT("string"),
			TEXT("Place name or address to geocode (required for 'geocode' operation)"), false),
		FMCPToolParameter(TEXT("lat"), TEXT("number"),
			TEXT("Latitude in decimal degrees (required for 'reverse_geocode' operation)"), false),
		FMCPToolParameter(TEXT("lon"), TEXT("number"),
			TEXT("Longitude in decimal degrees (required for 'reverse_geocode' operation)"), false),
	};
	Info.Annotations = FMCPToolAnnotations::ReadOnly();
	return Info;
}

// ---------------------------------------------------------------------------
// Execute — operation dispatch
// ---------------------------------------------------------------------------

FMCPToolResult FMCPTool_WebResearch::Execute(const TSharedRef<FJsonObject>& Params)
{
	// Step 1: Extract the required 'operation' discriminator.
	FString Operation;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("operation"), Operation, ParamError))
	{
		return ParamError.GetValue();
	}

	// Step 2: Dispatch to the appropriate handler.
	if (Operation == TEXT("search"))          return ExecuteSearch(Params);
	if (Operation == TEXT("fetch_page"))      return ExecuteFetchPage(Params);
	if (Operation == TEXT("geocode"))         return ExecuteGeocode(Params);
	if (Operation == TEXT("reverse_geocode")) return ExecuteReverseGeocode(Params);

	return FMCPToolResult::Error(FString::Printf(
		TEXT("Unknown operation '%s'. Expected: search | fetch_page | geocode | reverse_geocode"),
		*Operation));
}

// ---------------------------------------------------------------------------
// DoBlockingGet — synchronous HTTP GET helper
// ---------------------------------------------------------------------------

bool FMCPTool_WebResearch::DoBlockingGet(
	const FString& Url,
	const FString& UserAgent,
	FString& OutBody,
	FString& OutError,
	double TimeoutSec) const
{
	// Step 1: Build and configure the request.
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("GET"));
	Request->SetURL(Url);
	if (!UserAgent.IsEmpty())
	{
		Request->SetHeader(TEXT("User-Agent"), UserAgent);
	}

	// Step 2: Attach completion callback — captures by reference so we can
	//         read results after the spin-wait loop below.
	bool bDone = false;
	int32 ResponseCode = 0;
	Request->OnProcessRequestComplete().BindLambda(
		[&bDone, &OutBody, &ResponseCode, &OutError]
		(FHttpRequestPtr /*Req*/, FHttpResponsePtr Resp, bool bConnected)
		{
			if (bConnected && Resp.IsValid())
			{
				ResponseCode = Resp->GetResponseCode();
				OutBody      = Resp->GetContentAsString();
			}
			else
			{
				OutError = TEXT("Connection failed or response invalid");
			}
			bDone = true;
		});

	// Step 3: Fire the request.
	Request->ProcessRequest();

	// Step 4: Spin the HTTP manager on the game thread until done or timeout.
	const double Deadline = FPlatformTime::Seconds() + TimeoutSec;
	while (!bDone && FPlatformTime::Seconds() < Deadline)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.0f);
		FPlatformProcess::Sleep(0.05f);
	}

	// Step 5: Handle timeout.
	if (!bDone)
	{
		Request->CancelRequest();
		OutError = FString::Printf(TEXT("HTTP request timed out (%.0fs) for: %s"), TimeoutSec, *Url);
		return false;
	}

	// Step 6: Propagate connection-level failure.
	if (!OutError.IsEmpty())
	{
		return false;
	}

	// Step 7: Treat non-2xx as an error.
	if (ResponseCode < 200 || ResponseCode >= 300)
	{
		OutError = FString::Printf(TEXT("HTTP %d for %s"), ResponseCode, *Url);
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// ExtractDDGRealUrl — decode DuckDuckGo redirect href
// ---------------------------------------------------------------------------

FString FMCPTool_WebResearch::ExtractDDGRealUrl(const FString& Href) const
{
	// DDG wraps outbound links as: //duckduckgo.com/l/?uddg=<encoded>&rut=...
	// Extract and decode the uddg= query parameter to get the actual URL.
	const FString UddgKey = TEXT("uddg=");
	int32 KeyStart = Href.Find(UddgKey, ESearchCase::IgnoreCase);
	if (KeyStart != INDEX_NONE)
	{
		int32 ValueStart = KeyStart + UddgKey.Len();
		int32 AmpPos     = Href.Find(TEXT("&"), ESearchCase::IgnoreCase,
		                             ESearchDir::FromStart, ValueStart);
		FString Encoded  = (AmpPos != INDEX_NONE)
		                       ? Href.Mid(ValueStart, AmpPos - ValueStart)
		                       : Href.Mid(ValueStart);
		return FGenericPlatformHttp::UrlDecode(Encoded);
	}

	// Protocol-relative DDG link — prepend https.
	if (Href.StartsWith(TEXT("//")))
	{
		return FString(TEXT("https:")) + Href;
	}

	return Href;
}

// ---------------------------------------------------------------------------
// ExecuteSearch — DuckDuckGo HTML scrape
// ---------------------------------------------------------------------------

FMCPToolResult FMCPTool_WebResearch::ExecuteSearch(const TSharedRef<FJsonObject>& Params) const
{
	// Step 1: Extract parameters.
	FString Query;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("query"), Query, ParamError))
	{
		return ParamError.GetValue();
	}
	const int32 MaxResults = FMath::Clamp(
		ExtractOptionalNumber<int32>(Params, TEXT("max_results"), 5), 1, 20);

	// Step 2: Build DuckDuckGo HTML URL and perform blocking GET.
	const FString EncodedQuery = FGenericPlatformHttp::UrlEncode(Query);
	const FString Url = FString::Printf(
		TEXT("https://html.duckduckgo.com/html/?q=%s"), *EncodedQuery);
	const FString UA = FString::Printf(
		TEXT("UnrealClaude/%s (UE web_research tool)"), PluginVersion);

	FString ResponseBody;
	FString HttpError;
	if (!DoBlockingGet(Url, UA, ResponseBody, HttpError))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("web_research/search HTTP error: %s"), *HttpError));
	}

	// Step 3: Detect DuckDuckGo rate-limiting.
	if (ResponseBody.Contains(TEXT("rate limit"), ESearchCase::IgnoreCase) ||
	    ResponseBody.Contains(TEXT("ratelimit"),   ESearchCase::IgnoreCase))
	{
		return FMCPToolResult::Error(
			TEXT("DuckDuckGo rate-limited this IP. Wait a few seconds and retry."));
	}

	// Step 4: Parse result anchor elements using regex.
	// DDG HTML structure:
	//   <a class="result__a" href="//duckduckgo.com/l/?uddg=<enc>&...">Title</a>
	//   <a class="result__snippet" ...>Snippet text</a>
	//
	// Collect titles+urls first, then snippets, then zip them together.

	struct FDDGItem { FString Title; FString Url; FString Snippet; };
	TArray<FDDGItem> Items;

	// Parse result links (title + href).
	{
		const FRegexPattern LinkPattern(
			TEXT(R"REGEX(<a[^>]+class="result__a"[^>]+href="([^"]+)"[^>]*>([^<]+)</a>)REGEX"));
		FRegexMatcher LinkMatcher(LinkPattern, ResponseBody);
		while (LinkMatcher.FindNext() && Items.Num() < MaxResults)
		{
			FDDGItem Item;
			Item.Url   = ExtractDDGRealUrl(LinkMatcher.GetCaptureGroup(1));
			Item.Title = LinkMatcher.GetCaptureGroup(2).TrimStartAndEnd();
			// Decode any HTML entities in the title.
			Item.Title = Item.Title
				.Replace(TEXT("&amp;"),  TEXT("&"))
				.Replace(TEXT("&lt;"),   TEXT("<"))
				.Replace(TEXT("&gt;"),   TEXT(">"))
				.Replace(TEXT("&quot;"), TEXT("\""))
				.Replace(TEXT("&#x27;"), TEXT("'"));
			if (!Item.Url.IsEmpty() && !Item.Title.IsEmpty())
			{
				Items.Add(MoveTemp(Item));
			}
		}
	}

	// Parse snippets and pair them with the corresponding result by index.
	{
		const FRegexPattern SnippetPattern(
			TEXT(R"REGEX(<a[^>]+class="result__snippet"[^>]*>(.*?)</a>)REGEX"));
		FRegexMatcher SnippetMatcher(SnippetPattern, ResponseBody);
		int32 SnipIdx = 0;
		while (SnippetMatcher.FindNext() && SnipIdx < Items.Num())
		{
			FString Raw = SnippetMatcher.GetCaptureGroup(1).TrimStartAndEnd();
			// Strip inline tags (e.g. <b>, <span>).
			const FRegexPattern TagStrip(TEXT(R"(<[^>]+>)"));
			FRegexMatcher TagMatcher(TagStrip, Raw);
			FString Clean;
			int32 Pos = 0;
			while (TagMatcher.FindNext())
			{
				Clean += Raw.Mid(Pos, TagMatcher.GetMatchBeginning() - Pos);
				Pos = TagMatcher.GetMatchEnding();
			}
			Clean += Raw.Mid(Pos);
			Items[SnipIdx].Snippet = Clean
				.Replace(TEXT("&amp;"),  TEXT("&"))
				.Replace(TEXT("&lt;"),   TEXT("<"))
				.Replace(TEXT("&gt;"),   TEXT(">"))
				.Replace(TEXT("&hellip;"), TEXT("..."))
				.TrimStartAndEnd();
			++SnipIdx;
		}
	}

	// Step 5: Build result JSON.
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	for (const FDDGItem& Item : Items)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("title"),   Item.Title);
		Entry->SetStringField(TEXT("url"),     Item.Url);
		Entry->SetStringField(TEXT("snippet"), Item.Snippet);
		ResultsArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetArrayField(TEXT("results"),      ResultsArray);
	ResultData->SetNumberField(TEXT("result_count"), ResultsArray.Num());

	const FString Message = FString::Printf(
		TEXT("web_research/search: %d result(s) for \"%s\""), ResultsArray.Num(), *Query);
	return FMCPToolResult::Success(Message, ResultData);
}

// ---------------------------------------------------------------------------
// ExecuteFetchPage — Jina AI Reader
// ---------------------------------------------------------------------------

FMCPToolResult FMCPTool_WebResearch::ExecuteFetchPage(const TSharedRef<FJsonObject>& Params) const
{
	// Step 1: Extract parameters.
	FString TargetUrl;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("url"), TargetUrl, ParamError))
	{
		return ParamError.GetValue();
	}

	// Step 2: Build the Jina Reader URL — simply prefix the target URL.
	const FString JinaUrl = FString::Printf(TEXT("https://r.jina.ai/%s"), *TargetUrl);
	const FString UA = FString::Printf(
		TEXT("UnrealClaude/%s (UE web_research tool)"), PluginVersion);

	// Step 3: Perform blocking GET (allow up to 30s — Jina can be slow on large pages).
	FString ResponseBody;
	FString HttpError;
	if (!DoBlockingGet(JinaUrl, UA, ResponseBody, HttpError, 30.0))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("web_research/fetch_page HTTP error: %s"), *HttpError));
	}

	// Step 4: Extract the page title from the first "Title: ..." line that
	//         Jina inserts at the top of its markdown output.
	FString Title;
	{
		const FRegexPattern TitlePattern(TEXT(R"(^Title:\s*(.+)$)"));
		FRegexMatcher TitleMatcher(TitlePattern, ResponseBody);
		if (TitleMatcher.FindNext())
		{
			Title = TitleMatcher.GetCaptureGroup(1).TrimStartAndEnd();
		}
	}

	// Step 5: Build result JSON.
	const FString FetchedAt = FDateTime::UtcNow().ToString(TEXT("%Y-%m-%dT%H:%M:%SZ"));

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("title"),      Title);
	ResultData->SetStringField(TEXT("markdown"),   ResponseBody);
	ResultData->SetStringField(TEXT("fetched_at"), FetchedAt);
	ResultData->SetStringField(TEXT("source_url"), TargetUrl);

	const FString Message = FString::Printf(
		TEXT("web_research/fetch_page: fetched %s (%d chars)"),
		*TargetUrl, ResponseBody.Len());
	return FMCPToolResult::Success(Message, ResultData);
}

// ---------------------------------------------------------------------------
// ExecuteGeocode — Nominatim forward geocoding
// ---------------------------------------------------------------------------

FMCPToolResult FMCPTool_WebResearch::ExecuteGeocode(const TSharedRef<FJsonObject>& Params) const
{
	// Step 1: Extract parameters.
	FString PlaceName;
	TOptional<FMCPToolResult> ParamError;
	if (!ExtractRequiredString(Params, TEXT("place_name"), PlaceName, ParamError))
	{
		return ParamError.GetValue();
	}

	// Step 2: Build Nominatim search URL.
	// User-Agent is mandatory per Nominatim usage policy (returns 403 without it).
	const FString Encoded = FGenericPlatformHttp::UrlEncode(PlaceName);
	const FString Url = FString::Printf(
		TEXT("https://nominatim.openstreetmap.org/search?q=%s&format=json&limit=1"), *Encoded);
	const FString UA = FString::Printf(
		TEXT("UnrealClaude/%s (UE web_research tool; contact: unrealclaude@banming.dev)"),
		PluginVersion);

	// Step 3: Perform blocking GET.
	FString ResponseBody;
	FString HttpError;
	if (!DoBlockingGet(Url, UA, ResponseBody, HttpError))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("web_research/geocode HTTP error: %s"), *HttpError));
	}

	// Step 4: Parse JSON array response.
	TArray<TSharedPtr<FJsonValue>> ResultArray;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
		if (!FJsonSerializer::Deserialize(Reader, ResultArray) || ResultArray.Num() == 0)
		{
			return FMCPToolResult::Error(FString::Printf(
				TEXT("web_research/geocode: no results found for '%s'"), *PlaceName));
		}
	}

	// Step 5: Extract fields from the first (only) result.
	const TSharedPtr<FJsonObject>* FirstObj;
	if (!ResultArray[0]->TryGetObject(FirstObj) || !FirstObj || !(*FirstObj).IsValid())
	{
		return FMCPToolResult::Error(TEXT("web_research/geocode: malformed Nominatim response"));
	}

	FString LatStr, LonStr, DisplayName;
	(*FirstObj)->TryGetStringField(TEXT("lat"),          LatStr);
	(*FirstObj)->TryGetStringField(TEXT("lon"),          LonStr);
	(*FirstObj)->TryGetStringField(TEXT("display_name"), DisplayName);

	const double Lat = FCString::Atod(*LatStr);
	const double Lon = FCString::Atod(*LonStr);

	// Step 6: Build result JSON.
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetNumberField(TEXT("lat"),          Lat);
	ResultData->SetNumberField(TEXT("lon"),          Lon);
	ResultData->SetStringField(TEXT("display_name"), DisplayName);

	const FString Message = FString::Printf(
		TEXT("web_research/geocode: '%s' → (%.6f, %.6f)"), *PlaceName, Lat, Lon);
	return FMCPToolResult::Success(Message, ResultData);
}

// ---------------------------------------------------------------------------
// ExecuteReverseGeocode — Nominatim reverse geocoding
// ---------------------------------------------------------------------------

FMCPToolResult FMCPTool_WebResearch::ExecuteReverseGeocode(const TSharedRef<FJsonObject>& Params) const
{
	// Step 1: Extract lat and lon as numbers.
	// Use TryGetNumberField directly since ExtractRequiredString only handles strings.
	double Lat = 0.0, Lon = 0.0;
	if (!Params->TryGetNumberField(TEXT("lat"), Lat))
	{
		return FMCPToolResult::Error(
			TEXT("Missing required parameter: lat (must be a number)"));
	}
	if (!Params->TryGetNumberField(TEXT("lon"), Lon))
	{
		return FMCPToolResult::Error(
			TEXT("Missing required parameter: lon (must be a number)"));
	}

	// Step 2: Build Nominatim reverse URL.
	// User-Agent is mandatory per Nominatim usage policy.
	const FString Url = FString::Printf(
		TEXT("https://nominatim.openstreetmap.org/reverse?lat=%.10g&lon=%.10g&format=json"),
		Lat, Lon);
	const FString UA = FString::Printf(
		TEXT("UnrealClaude/%s (UE web_research tool; contact: unrealclaude@banming.dev)"),
		PluginVersion);

	// Step 3: Perform blocking GET.
	FString ResponseBody;
	FString HttpError;
	if (!DoBlockingGet(Url, UA, ResponseBody, HttpError))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("web_research/reverse_geocode HTTP error: %s"), *HttpError));
	}

	// Step 4: Parse the JSON object response.
	TSharedPtr<FJsonObject> NominatimObj;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
		if (!FJsonSerializer::Deserialize(Reader, NominatimObj) || !NominatimObj.IsValid())
		{
			return FMCPToolResult::Error(
				TEXT("web_research/reverse_geocode: failed to parse Nominatim response"));
		}
	}

	// Step 5: Check for Nominatim-level error (e.g., coordinates out of range).
	FString NominatimError;
	if (NominatimObj->TryGetStringField(TEXT("error"), NominatimError))
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("web_research/reverse_geocode Nominatim error: %s"), *NominatimError));
	}

	// Step 6: Extract the fields we expose.
	FString DisplayName;
	NominatimObj->TryGetStringField(TEXT("display_name"), DisplayName);

	const TSharedPtr<FJsonObject>* AddressObj;
	TSharedPtr<FJsonObject> Address;
	if (NominatimObj->TryGetObjectField(TEXT("address"), AddressObj) &&
	    AddressObj && (*AddressObj).IsValid())
	{
		Address = *AddressObj;
	}
	else
	{
		Address = MakeShared<FJsonObject>();
	}

	// Step 7: Build result JSON.
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("display_name"), DisplayName);
	ResultData->SetObjectField(TEXT("address"),      Address);

	const FString Message = FString::Printf(
		TEXT("web_research/reverse_geocode: (%.6f, %.6f) → \"%s\""),
		Lat, Lon, *DisplayName);
	return FMCPToolResult::Success(Message, ResultData);
}
