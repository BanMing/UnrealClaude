// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScopedTransaction.h"

/**
 * Thin RAII wrapper around FScopedTransaction so MCP write operations are
 * registered in the editor's undo/redo stack. Each write entry point should
 * begin a transaction with a human-readable description; the transaction
 * commits automatically when the returned shared pointer is destroyed
 * (i.e., when the calling function returns).
 *
 * Usage pattern in every MCP write function:
 *   TSharedPtr<FScopedTransaction> Tx = FMCPScopedTransaction::Begin(NSLOCTEXT(...));
 *   // ... perform modifications (Graph->Modify(), Node->Modify(), etc.) ...
 *   // Tx destructs at end of scope, committing the transaction.
 */
class FMCPScopedTransaction
{
public:
	/**
	 * Begin an undoable editor transaction.
	 *
	 * @param Description - Human-readable label shown in Edit > Undo / Edit > Redo
	 *                      menus after the operation completes (e.g., "MCP: Create blueprint node").
	 * @return Shared pointer owning the live transaction. Keep this pointer alive
	 *         for the entire duration of the write operation. The transaction commits
	 *         and is registered in the undo stack when the shared pointer is released.
	 */
	static TSharedPtr<FScopedTransaction> Begin(const FText& Description);
};
