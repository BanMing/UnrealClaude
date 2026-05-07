// Copyright Ban Ming. All Rights Reserved.
// Portions adapted from VibeUE (MIT) (c) 2025 Kevin Buckley / Buckley Builds LLC.
// https://github.com/buckleybuilds/VibeUE

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: web_research
 *
 * Compound tool for UE-process-internal web research and GPS geocoding.
 * All HTTP requests are synchronous blocking calls (up to 10s timeout) so
 * Execute() can return results directly on the game thread.
 *
 * Operations:
 *   search          — DuckDuckGo HTML search; returns titles, URLs, snippets
 *   fetch_page      — Jina AI Reader: fetches any URL as clean markdown (1 MB cap)
 *   geocode         — Nominatim: place name → lat/lon/display_name
 *   reverse_geocode — Nominatim: lat + lon → display_name + address object
 */
class FMCPTool_WebResearch : public FMCPToolBase
{
public:
	/**
	 * Return tool metadata, parameter declarations, and annotations.
	 * @return FMCPToolInfo describing the web_research tool.
	 */
	virtual FMCPToolInfo GetInfo() const override;

	/**
	 * Dispatch to the sub-operation indicated by the required "operation" param.
	 * @param Params - JSON object that must contain at minimum "operation".
	 * @return FMCPToolResult with structured JSON data on success, or an error.
	 */
	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;

private:
	// -------------------------------------------------------------------------
	// HTTP helper
	// -------------------------------------------------------------------------

	/**
	 * Perform a blocking HTTP GET request on the game thread.
	 * Spins the HttpManager at 50 ms intervals until the response arrives or
	 * TimeoutSec elapses.
	 *
	 * @param Url        - Fully-qualified URL to fetch.
	 * @param UserAgent  - Value for the User-Agent request header.
	 * @param OutBody    - Response body on success.
	 * @param OutError   - Human-readable error string on failure.
	 * @param TimeoutSec - Maximum seconds to wait before giving up (default 10).
	 * @return true if an HTTP 2xx response was received; false otherwise.
	 */
	bool DoBlockingGet(
		const FString& Url,
		const FString& UserAgent,
		FString& OutBody,
		FString& OutError,
		double TimeoutSec = 10.0) const;

	// -------------------------------------------------------------------------
	// Operation handlers
	// -------------------------------------------------------------------------

	/**
	 * Handle the "search" operation.
	 * Queries DuckDuckGo HTML endpoint and parses result anchors from the
	 * response HTML into a structured {results, result_count} payload.
	 *
	 * @param Params - Must contain "query"; optional "max_results" (default 5).
	 * @return FMCPToolResult with ResultData containing "results" array and
	 *         "result_count" number field.
	 */
	FMCPToolResult ExecuteSearch(const TSharedRef<FJsonObject>& Params) const;

	/**
	 * Handle the "fetch_page" operation.
	 * Routes the target URL through the Jina AI Reader (r.jina.ai) which
	 * returns the page as clean markdown. Response is capped at ~1 MB by Jina.
	 *
	 * @param Params - Must contain "url".
	 * @return FMCPToolResult with ResultData containing "title", "markdown",
	 *         "fetched_at", and "source_url".
	 */
	FMCPToolResult ExecuteFetchPage(const TSharedRef<FJsonObject>& Params) const;

	/**
	 * Handle the "geocode" operation.
	 * Sends a forward-geocoding request to Nominatim and returns the top hit.
	 *
	 * @param Params - Must contain "place_name".
	 * @return FMCPToolResult with ResultData containing "lat", "lon",
	 *         "display_name".
	 */
	FMCPToolResult ExecuteGeocode(const TSharedRef<FJsonObject>& Params) const;

	/**
	 * Handle the "reverse_geocode" operation.
	 * Sends a reverse-geocoding request to Nominatim for the given coordinates.
	 *
	 * @param Params - Must contain "lat" and "lon" as numbers.
	 * @return FMCPToolResult with ResultData containing "display_name" and
	 *         "address" object.
	 */
	FMCPToolResult ExecuteReverseGeocode(const TSharedRef<FJsonObject>& Params) const;

	// -------------------------------------------------------------------------
	// Parse helpers
	// -------------------------------------------------------------------------

	/**
	 * Extract the real destination URL from a DuckDuckGo redirect href.
	 * DDG wraps links as //duckduckgo.com/l/?uddg=<percent-encoded-url>&...
	 * This function decodes the uddg= value; if no uddg= is found the original
	 * href is returned unchanged.
	 *
	 * @param Href - Raw href value from DuckDuckGo HTML anchor.
	 * @return The decoded destination URL, or Href if no redirect detected.
	 */
	FString ExtractDDGRealUrl(const FString& Href) const;

	/** Version string embedded in User-Agent headers sent to external services. */
	static constexpr const TCHAR* PluginVersion = TEXT("1.4");
};
