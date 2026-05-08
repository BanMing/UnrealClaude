// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPScopedTransaction.h"

TSharedPtr<FScopedTransaction> FMCPScopedTransaction::Begin(const FText& Description)
{
	// Construct a new FScopedTransaction on the heap. The transaction is opened
	// immediately upon construction and committed when the last shared pointer
	// referencing it is released (RAII).
	return MakeShared<FScopedTransaction>(Description);
}
