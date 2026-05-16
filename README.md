# UnrealClaude

![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.7-313131?style=flat&logo=unrealengine&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat&logo=c%2B%2B&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-Win64%20%7C%20Linux%20%7C%20Mac-0078D6?style=flat&logo=windows&logoColor=white)
![Claude Code](https://img.shields.io/badge/Claude%20Code-Integration-D97757?style=flat&logo=anthropic&logoColor=white)
![MCP](https://img.shields.io/badge/MCP-46%20Tools-8A2BE2?style=flat)
![License](https://img.shields.io/badge/License-MIT-green?style=flat)

**Claude Code CLI integration for Unreal Engine 5.7** - Get AI coding assistance with built-in UE5.7 documentation context directly in the editor.

**Claude Code Plan changes PLEASE READ BEFORE JUNE 15TH TOO KEEP USING** 

[Changes annouced by Anthropic](https://support.claude.com/en/articles/15036540-use-the-claude-agent-sdk-with-your-claude-plan) might lead to extra usage billage when using the **custom in-editor chat box** . 
Because the plugin launches a MCP server on editor boot, **you can keep using UnrealClaude in claude code with the '/mcp' command.** 

> **Supported Platforms:** Windows (Win64), Linux, and macOS (Apple Silicon). Claude Opus 4.7 and its Claude Code release supported.
## Overview

UnrealClaude integrates the [Claude Code CLI](https://docs.anthropic.com/en/docs/claude-code) directly into the Unreal Engine 5.7 Editor and runs a MCP server for access to other coding agents.

<p align="center">
  <img width="45%" height="400" alt="Screenshot in editor chat" src="https://github.com/user-attachments/assets/5eff6f0d-8900-485c-b692-141bfb45d397" />
  <img width="45%" height="500" alt="Screenshot claude code" src="https://github.com/user-attachments/assets/abcd41ce-7caf-4e7b-87b8-39a406d29cd5" />
</p>


**Key Features:**
- **Native Editor Integration** - Chat panel docked in your editor with live streaming responses, tool call grouping, and code block rendering
- **MCP Server** - 30+ Model Context Protocol tools for actor manipulation, Blueprint editing, level management, UMG widgets + animation, material graphs + HLSL, input, and more
- **Dynamic UE 5.7 Context System** - The MCP bridge includes a dynamic context loader that provides accurate UE 5.7 API documentation on demand
- **Blueprint Editing** - Create and modify Blueprints, Animation Blueprints, state machines (Few bugs still, don't rely on fully)
- **Level Management** - Open, create, and manage levels and map templates programmatically
- **Asset Management** - Search assets, query dependencies and referencers
- **Async Task Queue** - Long-running operations won't timeout
- **Script Execution** - Claude can write, compile (via Live Coding), and execute scripts with your permission
- **Session Persistence** - Conversation history saved across editor sessions
- **Project-Aware** - Automatically gathers project context (modules, plugins, assets) and is able to see editor viewports
- **Uses Claude Code Auth** - No separate API key management needed

## Prerequisites

### 1. Install Claude Code CLI

```bash
npm install -g @anthropic-ai/claude-code
```

### 2. Authenticate Claude Code

```bash
claude auth login
```

This will open a browser window to authenticate with your Anthropic account (Claude Pro/Max subscription) or set up API access.

### 3. Verify Installation

```bash
claude --version
claude -p "Hello, can you see me?"
```

## Installation

<img width="1222" height="99" alt="Screenshot 2026-02-06 112433" src="https://github.com/user-attachments/assets/61d72364-f7bc-4f34-a768-aedc0f5cea2e" />

(Check the Editor category in the plugin browser. You might need to scroll down for it if search doesn't pick it up)

### Step 1: Clone and Build

This plugin must be built from source for your platform and engine version. No prebuilt binaries are included.

1. Clone this repository (includes the MCP bridge submodule):
   ```bash
   git clone --recurse-submodules https://github.com/Natfii/UnrealClaude.git
   ```
   If you already cloned without `--recurse-submodules`, run:
   ```bash
   cd UnrealClaude
   git submodule update --init
   ```

2. Build the plugin:

   **Windows:**
   ```bash
   Engine\Build\BatchFiles\RunUAT.bat BuildPlugin -Plugin="PATH\TO\UnrealClaude\UnrealClaude\UnrealClaude.uplugin" -Package="OUTPUT\PATH" -TargetPlatforms=Win64
   ```

   **Linux:**
   ```bash
   Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin="/path/to/UnrealClaude/UnrealClaude/UnrealClaude.uplugin" -Package="/output/path" -TargetPlatforms=Linux
   ```

   **macOS:**
   ```bash
   Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin="/path/to/UnrealClaude/UnrealClaude/UnrealClaude.uplugin" -Package="/output/path" -TargetPlatforms=Mac
   ```

### Step 2: Install the Plugin

Copy the built plugin to either your **project** or **engine** plugins folder.

**Option A: Project Plugin (Recommended)**

Copy the build output to your project's `Plugins` directory:
```
YourProject/
├── Content/
├── Source/
└── Plugins/
    └── UnrealClaude/
        ├── Binaries/
        ├── Source/
        ├── Resources/
        ├── Config/
        └── UnrealClaude.uplugin
```

**Option B: Engine Plugin (All Projects)**

Copy to your engine's plugins folder:

**Windows:**
```
C:\Program Files\Epic Games\UE_5.7\Engine\Plugins\Marketplace\UnrealClaude\
```

**Linux:**
```
/path/to/UnrealEngine/Engine/Plugins/Marketplace/UnrealClaude/
```

### Step 3: Install MCP Bridge Dependencies

Required for Blueprint tools and editor integration:
```bash
cd <PluginPath>/UnrealClaude/Resources/mcp-bridge
npm install
```

### Step 4: Launch

Launch the editor - the plugin will load automatically.


### Step 5: (Optional but recommend): Connect Claude Code CLI to use Unreal without the chat box 

1. **Launch Claude Code or Claude CLI** and point it to your UnrealClaude install. 

<img width="1097" height="154" alt="image" src="https://github.com/user-attachments/assets/05a88a96-abce-4ebb-93ef-1ea405e5f28a" />


2. After the all clear, **restart just Claude Code / CLI for changes to save.** 

<p align="center">
  <img height="200" alt="Screenshot 1" src="https://github.com/user-attachments/assets/251f1580-aec2-415c-abab-6a2e50662a8e" />
  <img height="200" alt="Screenshot 2" src="https://github.com/user-attachments/assets/ffda3096-d808-41e7-85ba-176dfe6385d8" />
</p>



## macOS Quick Start (Apple Silicon)

For full details, see [INSTALL_MAC.md](INSTALL_MAC.md).

1. **Install Node.js and Claude Code CLI:**
   ```bash
   brew install node
   npm install -g @anthropic-ai/claude-code
   claude
   ```
2. **Install the plugin** into your project's `Plugins/` directory
3. **Install MCP bridge dependencies:**
   ```bash
   cd YourProject/Plugins/UnrealClaude/Resources/mcp-bridge
   npm install
   ```
4. **Launch** the editor and open **Tools > Claude Assistant**

## Linux Quick Start (Rocky/Fedora)

For full details, see [INSTALL_LINUX.md](INSTALL_LINUX.md).

1. **Install Libraries:**
   ```bash
   sudo dnf install -y nss nspr mesa-libgbm libXcomposite libXdamage libXrandr alsa-lib pciutils-libs libXcursor atk at-spi2-atk pango cairo gdk-pixbuf2 gtk3
   ```
2. **Install Clipboard Support:**
   ```bash
   sudo dnf install -y wl-clipboard   # Wayland
   sudo dnf install -y xclip          # X11 fallback
   ```
3. **Setup Wayland:**
   ```bash
   export SDL_VIDEODRIVER=wayland
   export UE_Linux_EnableWaylandNative=1
   ```
4. **Build and Launch:**
   ```bash
   ./UnrealEditor -vulkan
   ```

## Usage

### Opening the Claude Panel

 Menu → Tools → Claude Assistant

<img width="580" height="340" alt="{778C8E0B-C354-4AD1-BBFF-B514A4D5FC16}" src="https://github.com/user-attachments/assets/2087ef40-9791-4ad9-933b-2c64370344e8" />


### Example Prompts

```
How do I create a custom Actor Component in C++?

What's the best way to implement a health system using GAS?

Explain World Partition and how to set up streaming for an open world.

Write a BlueprintCallable function that spawns particles at a location.

How do I properly use TObjectPtr<> vs raw pointers in UE5.7?
```

### Input Shortcuts

| Shortcut | Action |
|----------|--------|
| `Enter` | Send message |
| `Shift+Enter` | New line in input |
| `Escape` | Cancel current request |

## Features

### Session Persistence

Conversations are automatically saved to your project's `Saved/UnrealClaude/` directory and restored when you reopen the editor. The plugin maintains conversation context across sessions.

### Project Context

UnrealClaude automatically gathers information about your project:
- Source modules and their dependencies
- Enabled plugins
- Project settings
- Recent assets
- Custom CLAUDE.md instructions

### Scripting

<img width="707" height="542" alt="{AB6AC101-4A4C-4607-BFB6-187D49F5E65B}" src="https://github.com/user-attachments/assets/e0c2e398-8fcd-4ac6-ade7-d50870215ec1" />

### MCP Server

The plugin includes a Model Context Protocol (MCP) server with 30+ tools that expose editor functionality to Claude and external tools. The MCP server runs on port 8765 by default and starts automatically when the editor loads.

**Tool Categories:**
- **Actor Tools** - Spawn, move, delete, inspect, and set properties on actors
- **Level Management** - Open levels, create new levels from templates, list available templates
- **Blueprint Tools** - Create and modify Blueprints (variables, functions, custom events, nodes, pins); supports `DynamicCast` / `Knot` / `SwitchEnum` / `MakeArray` node types and `TMap<K,V>` pin types; cursor-driven auto-layout when `pos_x` / `pos_y` are omitted; all writes are wrapped in `FScopedTransaction` so they appear in Edit > Undo
- **Animation Blueprint Tools** - Full state machine editing (states, transitions, conditions, batch operations)
- **Asset Tools** - Search assets, query dependencies and referencers with pagination
- **Character Tools** - Character configuration, movement settings, and data queries
- **Material Tools** - Material and material instance parameter operations
- **UMG Widget Tools** *(new)* - Read & mutate Widget Blueprint trees: query, create, reparent, delete, set properties (with `FSlateBrush.ResourceObject` reflection-safe path)
- **UMG Animation Tools** *(new)* - Author UWidgetAnimation keyframes from JSON: Float / Color / Vector2D tracks, automatic possessable binding, TickResolution-correct frame conversion
- **Material Graph + HLSL Tools** *(new)* - Edit `UMaterial` graphs node-by-node (add/connect/set node properties/compile) and rewrite Custom-node HLSL bodies with auto-rebuilt `FCustomInput` arrays
- **PIE Control Tools** *(new)* - Start/stop/pause/resume Play-In-Editor sessions; inject key/action/axis/move/look input; wait for state transitions
- **Native Build Tools** *(new)* - Trigger Live Coding for non-layout C++ patches; spawn `Build.bat` + auto-relaunch for USTRUCT/UCLASS layout changes
- **StateTree Tools** *(new)* - Query states/transitions/tasks/evaluators/parameters; mutate via add_state / add_task / add_transition / remove_state
- **UMG Session Anchor** *(new)* - Implicit current-widget target shared across umg_query / umg_modify / umg_animation
- **Asset Management Tools** *(new)* - Full asset CRUD: search / find / list_folder / open / save / duplicate / move / reference-aware delete
- **Log Reader Tools** *(new)* - Direct `Saved/Logs/*.log` access: tail / head / regex filter / errors / warnings / incremental since-cursor
- **Web Research Tools** *(new)* - DuckDuckGo search + Jina AI Reader markdown fetch + OpenStreetMap Nominatim geocoding (no API keys)
- **Niagara Tools** *(new)* - List systems, inspect emitters, spawn at location, set float/vec3/color parameters
- **GAS Tools** *(new)* - Inspect AttributeSets / Abilities / Effects, create BPs with project-subclass parents, set ability tags + effect modifiers
- **Enhanced Input Tools** - Input action and mapping context management
- **Utility Tools** - Console commands, output log, viewport capture, script execution
- **Async Task Queue** - Background execution for long-running operations

For full MCP tool documentation with parameters, examples, and API details, see [UnrealClaude's MCP Bridge](https://github.com/Natfii/ue5-mcp-bridge) repository.

#### UMG, Animation & Material Authoring (added 2026-05)

Three new tool families ported from [UnrealMotionGraphicsMCP (UmgMcp, MIT)](https://github.com/winyunq/UnrealMotionGraphicsMCP) and adapted to the UnrealClaude tool registry. All are fully stateless — every call takes the explicit asset path; no global "current target" subsystem.

**Story 1 — UMG Widget CRUD** (`umg_query` / `umg_modify`):

| Tool | Operations |
|------|-----------|
| `umg_query` | `get_widget_tree`, `query_widget_properties`, `get_widget_schema`, `get_layout_data`, `get_creatable_widget_types` |
| `umg_modify` | `create_widget`, `set_widget_properties`, `delete_widget`, `reparent_widget`, `save_asset` |

Load-bearing details preserved from the upstream MIT source: `WidgetVariableNameToGuidMap` registration on every variable widget, `FSlateBrush.ResourceObject` reflection interception (FJsonObjectConverter is unstable on this property), separate Slot vs Widget property apply paths, automatic root promotion on empty trees, and `MarkBlueprintAsStructurallyModified` after every write.

**Story 2 — Material Graph + HLSL** (`material_graph` / `material_hlsl`):

| Tool | Operations |
|------|-----------|
| `material_graph` | `set_target`, `define_variable`, `add_node`, `delete_node`, `connect_nodes`, `connect_pins`, `set_node_properties`, `get_node_info`, `get_graph`, `set_output_node`, `compile_asset` |
| `material_hlsl` | `hlsl_set_target`, `hlsl_get`, `hlsl_set`, `hlsl_compile` |

Root-pin smart aliases preserved (`Output`/`FinalColor` → `EmissiveColor` for UI domain, `BaseColor` for surface). Custom HLSL writes auto-rebuild the `FCustomInput` array and call `PostEditChange()` + `ForceRefreshMaterialEditor()`. The two tools are intentionally split because HLSL is a text workflow and graph editing is a node workflow — different LLM mental models.

**Story 3 — UMG Animation Keyframes** (`umg_animation`):

| Operation | Description |
|-----------|-------------|
| `get_all_animations` | List every `UWidgetAnimation` on a widget blueprint (optional `detailed: true` adds per-widget track + key-count breakdown) |
| `create_animation` / `delete_animation` | Find-or-create / destructive remove (requires `confirm_delete`) |
| `get_animation_keyframes` | Dump every track + key for a named animation |
| `get_widget_animation_data` | Per-widget timeline (filtered Float / Color / Vector2D) |
| `set_property_keys` | Upsert keys on a property track (auto-detects type from key shape) |
| `remove_property_track` / `remove_keys` | Drop the entire track or specific keys (`confirm_delete`) |
| `append_widget_tracks` | Batch wrapper: per widget, multiple tracks |
| `set_animation_data` | L2 batch wrapper: widget + tracks list |
| `sample_at_time` *(PR-D)* | Evaluate every track at one or more query times (interpolated) |
| `append_time_slice` *(PR-D)* | Write keys for many widgets+properties at a single time |

Track type is auto-detected from `keys[*].value` shape: `number` → `UMovieSceneFloatTrack`, `{r,g,b,a}` → `UMovieSceneColorTrack`, `{x,y}` → `UMovieSceneDoubleVectorTrack(NumChannels=2)`. Frame conversion uses `MovieScene->GetTickResolution()` (typically 60000 fps), **not** a hardcoded 60 Hz. Possessable bindings + `WidgetVariableNameToGuidMap` are auto-created for unknown widgets, and `PlaybackRange` is auto-extended on every key write so late-timeline keys don't get clipped.

> Attribution: Portions adapted from [UmgMcp (MIT)](https://github.com/winyunq/UnrealMotionGraphicsMCP) © 2025-2026 Winyunq. The original `UmgAttentionSubsystem` global-state model was dropped — every operation requires an explicit `widget_blueprint_path` / `animation_name` / `widget_name` so calls are reproducible across sessions.

#### PIE / Build / UMG Anchor / StateTree (added 2026-05)

Five tools added in the PR-A / PR-B / PR-E rounds covering Play-In-Editor lifecycle, native-code rebuild, UMG implicit-target state, and StateTree authoring.

**`pie_session`** *(PR-A)* — control a Play-In-Editor session:

| Action | Purpose |
|--------|---------|
| `start` | Begin PIE in `viewport` / `new_window` / `standalone` mode |
| `stop` / `pause` / `resume` | Lifecycle control |
| `get_state` | Inspect current PIE state (running / paused / stopped) |
| `wait_for` | Block until a specified state is reached |

**`pie_input`** *(PR-A)* — inject input into the active PIE session (`key` / `action` / `axis` / `move_to` / `look_at`), targeting `player_index` 0+.

**`trigger_live_coding`** *(PR-A)* — kick off an Unreal Live Coding recompile (`Ctrl+Alt+F11`) for native C++ patches that don't change reflection layout.

**`build_and_relaunch`** *(PR-A)* — for changes Live Coding cannot patch (USTRUCT layout, UCLASS hierarchy, new UPROPERTY): spawn `Build.bat` against the editor target, then relaunch the editor on the same project.

**`umg_session`** *(PR-B)* — manage the UMG session anchor (the implicit current widget used by `umg_query` / `umg_modify` / `umg_animation` when `widget_blueprint_path` is omitted):

| Operation | Purpose |
|-----------|---------|
| `get_target` / `set_target` | Read or write the current target |
| `get_last_edited` | Last widget the editor opened in-process |
| `get_recently_edited` | Stack of recently opened widgets |

State lives in `UUMGSessionSubsystem` (editor subsystem), so the anchor survives across MCP calls within the same editor process.

**`statetree_query`** *(PR-E)* — read-only inspection of `UStateTree` assets. Sections selectable via `include`: `states` / `transitions` / `tasks` / `evaluators` / `parameters` / `all`. `detailed: true` adds per-state `selection_behavior` / `depth` / `parent` / `children` / `enabled` / task-and-transition begin indices.

**`statetree_modify`** *(PR-E)* — compound StateTree authoring:

| Operation | Purpose |
|-----------|---------|
| `add_state` | Create a new state under root or a parent (`State` / `Group` / `Linked` / `Subtree`) |
| `add_task` | Attach a `FStateTreeTaskBase` subclass to a state |
| `add_transition` | Add a transition (target may be a state name or `Succeeded` / `Failed` / `Next`) |
| `remove_state` | Remove state + children (`confirm_delete: true` required) |

Read-before-write convention: always call `statetree_query` first to confirm asset path + state names. Every mutation wraps `FScopedTransaction` + `Modify()` on StateTree + EditorData + affected state, then `MarkPackageDirty()`.

> Attribution: PR-A + PR-E adapted from [yes-ue-mcp (MIT)](https://github.com/softdaddy-o/yes-ue-mcp) © 2024 softdaddy-o. PR-B `umg_session` adapted from [UmgMcp (MIT)](https://github.com/winyunq/UnrealMotionGraphicsMCP) © 2025-2026 Winyunq.

#### Asset / Logs / Web / Niagara / GAS (added 2026-05)

Five compound tools ported from [VibeUE (MIT)](https://github.com/buckleybuilds/VibeUE) and [ue-mcp (BUSL-1.1)](https://github.com/davidlyon/ue-mcp), adapted to the UnrealClaude tool registry. All follow the existing compound-tool convention (single tool + `operation` parameter dispatch) so the LLM tool list stays compact.

**`asset_manage`** — full asset CRUD with reference-aware operations:

| Operation | Purpose |
|-----------|---------|
| `search` | Asset Registry search by name / type / path |
| `find` | Resolve asset path → existence + class + package |
| `list_folder` | List assets in a folder, optionally recursive + class filter |
| `open_in_editor` | Open the asset in its editor window |
| `save` / `save_all_dirty` | Save a single asset or every dirty package |
| `duplicate` | Duplicate to a new path (refs preserved) |
| `move` | Rename / move with referencer redirector updates |
| `delete` | Reference-aware delete (`confirm_delete: true`, `force?` to break refs) |

**`logs_read`** — direct access to `Saved/Logs/*.log` (complements the in-memory `get_output_log`):

| Operation | Purpose |
|-----------|---------|
| `list` / `info` | Enumerate log files / inspect size + line count |
| `read` / `tail` / `head` | Pagination, last N, first N |
| `filter` | Regex match across the file (`FRegexMatcher`) |
| `errors` / `warnings` | Parse `LogXxx: Error: ...` / `Warning: ...` lines with category |
| `since` | Incremental fetch from a saved cursor |

**`web_research`** — in-editor HTTP via `FHttpModule` (no API keys required):

| Operation | Backend |
|-----------|---------|
| `search` | DuckDuckGo HTML scrape (no key) |
| `fetch_page` | Jina AI Reader (`https://r.jina.ai/<url>`) → markdown |
| `geocode` / `reverse_geocode` | OpenStreetMap Nominatim (sends required `User-Agent: UnrealClaude/1.x`) |

> Attribution: PR-F three tools adapted from [VibeUE (MIT)](https://github.com/buckleybuilds/VibeUE) © 2025 Kevin Buckley / Buckley Builds LLC.

**`niagara_modify`** — Niagara System inspection + parameter authoring:

| Operation | Purpose |
|-----------|---------|
| `list_systems` | Enumerate `UNiagaraSystem` assets under a path |
| `get_info` | Emitters + exposed parameters of a system |
| `spawn_at_location` | `UNiagaraFunctionLibrary::SpawnSystemAtLocation` with auto-destroy |
| `set_parameter` | `SetVariableFloat` / `SetVariableVec3` / `SetVariableLinearColor` (auto-detect from value shape) |

**`gas_modify`** — Gameplay Ability System asset CRUD with project-subclass support:

| Operation | Purpose |
|-----------|---------|
| `list_attribute_sets` / `list_abilities` / `list_effects` | Enumerate GAS assets with tags + modifier metadata |
| `create_ability_blueprint` | Create a new `UGameplayAbility` BP, supports project subclasses (e.g. `UPaogeGameplayAbility`) via `parent_class` |
| `create_attribute_set_blueprint` | Create a new `UAttributeSet` BP with declared attributes |
| `create_effect_blueprint` | Create a new `UGameplayEffect` BP with duration policy |
| `set_ability_tags` | Overwrite `AbilityTags` / `CancelAbilitiesWithTag` / `BlockAbilitiesWithTag` |
| `set_effect_modifier` | Append a `FGameplayModifierInfo` (Add / Multiply / Override) |

Load-bearing UE 5.7 quirks captured: `UGameplayAbility::AbilityTags` is deprecated → tags are written via `FStructProperty` reflection on the backing `AssetTags` field. `EGameplayModOp::Multiplicitive` (engine typo, **not** Multiplicative) is the correct enum spelling for multiply modifiers. `CancelAbilitiesWithTag` / `BlockAbilitiesWithTag` are `protected` in 5.7 and also written via `FindFProperty<FStructProperty>` + `ContainerPtrToValuePtr`. Every BP write wraps `FScopedTransaction` + `FKismetEditorUtilities::CompileBlueprint` + `MarkPackageDirty` so undo and asset-cache stay coherent.

> Attribution: PR-G two tools adapted from [ue-mcp (BUSL-1.1)](https://github.com/davidlyon/ue-mcp) © 2024 David Lyon. BUSL-1.1 commercial use is project-owner authorized.

#### Blueprint Authoring + Undo (added 2026-05-08)

A round of Blueprint-graph improvements layered on top of the existing `blueprint_modify` tool. None of these changes alter the public schema beyond accepting new operation / node-type strings — existing callers keep working unchanged.

**`add_custom_event` operation** — logic-light alternative to `add_function`:

| Param | Purpose |
|-------|---------|
| `function_name` | Name of the new custom event (re-uses the same key as `add_function` so callers can swap ops without re-keying) |
| `blueprint_path` | Standard Blueprint load context |

Drops a `UK2Node_CustomEvent` directly into `UbergraphPages[0]` (the canonical Event Graph). This avoids the `FBlueprintEditorUtils::AddFunctionGraph` code path that has been observed to crash for `UWidgetBlueprint` on certain UE 5.7 builds. Suitable for UI button handlers and other fire-and-forget entry points that do not need parameter pins, return nodes, or recursion. Duplicate event names in the same graph are rejected.

**New `add_node` types** — extends `node_type` beyond the original CallFunction / Branch / Event / VariableGet / VariableSet / Sequence / math set:

| Node Type | Aliases | Required `node_params` |
|-----------|---------|------------------------|
| `DynamicCast` | `Cast` | `target_class` (short or path-qualified UClass name, e.g. `Pawn`, `PaogeCharacter`) |
| `Knot` | `Reroute` | none — wire passthrough |
| `SwitchEnum` | `Switch` | `enum` (short or path-qualified UEnum name; output pins reconstructed from enum values) |
| `MakeArray` | — | `element_type` (optional; empty = wildcard, type inferred from first connection) |

**Cursor-driven auto-layout** — `add_node` calls that omit BOTH `pos_x` and `pos_y` consume the session cursor's current slot, then advance X by 250 graph units. A sequence of position-less calls produces a left-to-right ribbon instead of stacking every node at `(0, 0)`. Switching graphs implicitly resets the cursor (the old program-counter id refers to a node in a different graph and would silently produce cross-graph wiring errors if reused).

The cursor lives inside `UUMGSessionSubsystem` alongside the existing UMG anchor — the subsystem name is kept for backward source-compat but it now tracks two orthogonal session concerns:

| State | Purpose |
|-------|---------|
| `CurrentTargetAssetPath` + `RecentlyEditedHistory` | UMG anchor (existing behavior — implicit `widget_blueprint_path`) |
| `CurrentGraphName` + `bCurrentGraphIsFunction` | Anchored graph for cursor tracking |
| `CurrentCursorNodeId` | Program-counter — id of the most recently added node |
| `CurrentCursorPosition` | Visual cursor in graph-space coordinates |

The `add_node` response is augmented with `pos_x` / `pos_y` (the resolved coordinates) and `used_cursor: bool` so callers can verify whether the cursor was consumed.

**`TMap<K, V>` pin types** — `FBlueprintEditor::ParseContainerType` and `PinTypeToString` now round-trip map types. The key drives the primary `FEdGraphPinType` fields; the value is packed into `PinValueType` via `FEdGraphTerminalType::FromPinType`. Keys and values must both be scalar (UE Blueprint forbids nested containers in either slot — error is surfaced to the caller). Brace-depth-tracking comma split handles nested templates such as `TMap<FName, TArray<int32>>` correctly.

**Undo / Redo via `FMCPScopedTransaction`** — a thin RAII wrapper around `FScopedTransaction` is now opened by every Blueprint-graph mutation:

- `CreateNode` — wraps node creation; the `Graph->Modify()` and `NewNode->Modify()` calls record the pre-state
- `DeleteNode` — wraps the destructive path; both graph and node `Modify()` are recorded before connection breaks
- `ConnectPins` — opened after node validation (so aborted calls do not pollute the undo stack), before any pin mutation; both involved nodes `Modify()`
- `DisconnectPins` — same shape as `ConnectPins`

Result: every MCP write shows up as a labelled entry in Edit > Undo / Edit > Redo (e.g. `MCP: Create blueprint node`, `MCP: Connect blueprint pins`). Aborted operations that fail validation do not leave empty entries on the undo stack.

**`asset` domain router split** — `tool-router.js` in the `mcp-bridge` submodule now dispatches the `asset` domain to two backing tools based on operation:

| Backing Tool | Operations |
|--------------|-----------|
| `FMCPTool_Asset` (property / save / inspect) | `set_asset_property`, `save_asset`, `get_asset_info`, `list_assets` |
| `FMCPTool_AssetManage` (CRUD / search / referencer-aware delete) | `search`, `find`, `list_folder`, `open_in_editor`, `save_all_dirty`, `duplicate`, `move`, `delete` |

Caller-facing schema still advertises a single `asset` domain — the split is internal. `delete` still requires `confirm_delete: true` and is blocked by referencers unless `force: true` is also passed.

> Attribution: `add_custom_event` and the cursor-position model adapted from [UmgMcp (MIT)](https://github.com/winyunq/UnrealMotionGraphicsMCP) © 2025-2026 Winyunq. The original `UmgAttentionSubsystem` design merged into the existing `UUMGSessionSubsystem` rather than introducing a parallel subsystem.

#### Dynamic UE 5.7 Context System

The MCP bridge includes a dynamic context loader that provides accurate UE 5.7 API documentation on demand. Use `unreal_get_ue_context` to query by category (animation, blueprint, slate, actor, assets, replication) or search by keywords. Context status is shown in `unreal_status` output.

## Configuration

### Project Settings

In **Project Settings → Plugins → Unreal Claude**:

- **Auto-approve script execution** (default: OFF) — when ON, every Python / C++ / Console / Editor Utility script triggered through the MCP bridge or the in-editor chat runs immediately without showing the permission dialog. Each auto-approved script writes a `LogUnrealClaude` Log entry with its type and description so the audit trail is preserved. Designed for trusted MCP-driven / agent-driven workflows where the per-script confirmation becomes dominant friction; **only enable on machines and projects where the connected client is trusted**.

The setting is persisted to `Config/DefaultEditor.ini` under `[/Script/UnrealClaude.UnrealClaudeSettings]`.

### Custom System Prompts

You can extend the built-in UE5.7 context by creating a `CLAUDE.md` file in your project root:

```markdown
# My Project Context

## Architecture
- This is a multiplayer survival game
- Using Dedicated Server model
- GAS for all abilities

## Coding Standards
- Always use UPROPERTY for Blueprint access
- Prefix interfaces with I (IInteractable)
- Use GameplayTags for ability identification
```

### Allowed Tools

By default, the plugin runs Claude with these tools: `Read`, `Write`, `Edit`, `Grep`, `Glob`, `Bash`. You can modify this in `ClaudeSubsystem.cpp`:

```cpp
Config.AllowedTools = { TEXT("Read"), TEXT("Grep"), TEXT("Glob") }; // Read-only
```

## How It Works

1. User enters a prompt in the editor widget
2. Plugin builds context from UE5.7 knowledge + project information
3. Executes: `claude -p --skip-permissions --append-system-prompt "..." "your prompt"`
4. Claude Code runs with your project as the working directory
5. Response is captured and displayed in the chat panel
6. Conversation is persisted for future sessions

### Command Line Equivalent

```bash
cd "C:\YourProject"
claude -p --skip-permissions \
  --allowedTools "Read,Write,Edit,Grep,Glob,Bash" \
  --append-system-prompt "You are an expert Unreal Engine 5.7 developer..." \
  "How do I create a custom GameMode?"
```

## Troubleshooting

### "Claude CLI not found"

1. Verify Claude is installed: `claude --version`
2. Check it's in your PATH: `where claude`
3. Restart Unreal Editor after installation

### "Authentication required"

Run `claude auth login` in a terminal to authenticate.

### Responses are slow

Claude Code executes in your project directory and may read files for context. Large projects may have slower initial responses.

You might also have too many global Claude Code plugins enabled (i.e. Superpowers, ralp-loop, context7). The context for those plugins 
getting injected can cause slowdowns up to 3+ minutes. 

### Plugin doesn't compile

Ensure you're on Unreal Engine 5.7. Supported platforms are Windows (Win64), Linux, and macOS.

### MCP Server not starting

Check if port 8765 is available. The MCP server logs to `LogUnrealClaude`.

### MCP tools not available / Blueprint tools not working

If Claude says the MCP tools are in its instructions but not in its function list:

1. **Install MCP bridge dependencies**: The most common cause is missing npm packages:
   ```bash
   cd YourProject/Plugins/UnrealClaude/Resources/mcp-bridge
   npm install
   ```

2. **Verify the HTTP server is running**: With the editor open, test:
   ```bash
   curl http://localhost:8765/mcp/status
   ```
   You should see a JSON response with project info.

3. **Check the Output Log**: Look for `LogUnrealClaude` messages:
   - `MCP Server started on http://localhost:8765` - Server is running
   - `Registered X MCP tools` - Tools are loaded

4. **Restart the editor**: After installing npm dependencies, restart Unreal Editor.

### Request Hangs 

New watchdog should warn about hung requests (60s). Make sure all files are on disk (DISABLE OneDrive, DropBox, etc.)

### Debugging the MCP Bridge

The MCP bridge is also available as a [standalone repository](https://github.com/Natfii/ue5-mcp-bridge) with its own Vitest test suite. If you're experiencing bridge-level issues (tool listing, parameter translation, context injection), you can run the bridge tests independently:

```bash
cd path/to/ue5-mcp-bridge
npm install
npm test
```

This tests the bridge without requiring a running Unreal Editor.


## Contributing

Feel free to fork for your own needs! Possible areas for improvement:

- [x] Linux support (thanks [@bearyjd](https://github.com/bearyjd))
- [x] Mac support (thanks [@lateralsummer](https://github.com/lateralsummer))
- [ ] Additional MCP tools (current tools need refractoring, no new ones for now)

## License

MIT License - See [LICENSE](UnrealClaude/LICENSE) file.

## Credits

- Built for Unreal Engine 5.7
- Integrates with [Claude Code](https://claude.ai/code) by Anthropic
