// Copyright (c) 2015, tandasat. All rights reserved.
// Use of this source code is governed by a MIT-style license that can be
// found in the LICENSE file.

//
// This module implements an entry point of the driver and initializes other
// components in this module.
//
#include "stdafx.h"
#include "ssdt.h"
#include "log.h"
#include "util.h"

namespace stdexp = std::experimental;

////////////////////////////////////////////////////////////////////////////////
//
// macro utilities
//

////////////////////////////////////////////////////////////////////////////////
//
// constants and macros
//

////////////////////////////////////////////////////////////////////////////////
//
// types
//

struct SERVICE_DESCRIPTOR_TABLE {
  PULONG ServiceTable;
  PULONG CounterTable;
  ULONG_PTR TableSize;
  PUCHAR ArgumentTable;
};
static_assert(sizeof(SERVICE_DESCRIPTOR_TABLE) == sizeof(void *) * 4,
              "Size check");

////////////////////////////////////////////////////////////////////////////////
//
// prototypes
//

EXTERN_C PVOID NTAPI
RtlPcToFileHeader(_In_ PVOID PcValue, _Out_ PVOID *BaseOfImage);

EXTERN_C static SERVICE_DESCRIPTOR_TABLE *SSDTpFindTable();

////////////////////////////////////////////////////////////////////////////////
//
// variables
//

static SERVICE_DESCRIPTOR_TABLE *g_SSDTpTable = nullptr;

////////////////////////////////////////////////////////////////////////////////
//
// implementations
//

ALLOC_TEXT(INIT, SSDTInitialization)
EXTERN_C NTSTATUS SSDTInitialization() {
  PAGED_CODE();

  g_SSDTpTable = SSDTpFindTable();
  if (!g_SSDTpTable) {
    return STATUS_UNSUCCESSFUL;
  }
  return STATUS_SUCCESS;
}

ALLOC_TEXT(PAGED, SSDTTermination)
EXTERN_C void SSDTTermination() { PAGED_CODE(); }

ALLOC_TEXT(PAGED, SSDTGetProcAdderss)
EXTERN_C FARPROC SSDTGetProcAdderss(_In_ ULONG Index) {
  PAGED_CODE();

  if (IsX64()) {
    return reinterpret_cast<FARPROC>(
        (g_SSDTpTable->ServiceTable[Index] >> 4) +
        reinterpret_cast<ULONG_PTR>(g_SSDTpTable->ServiceTable));
  } else {
    return reinterpret_cast<FARPROC>(g_SSDTpTable->ServiceTable[Index]);
  }
}

// Get an original value of the SSDT and replace it with a new value.
EXTERN_C void SSDTSetProcAdderss(_In_ ULONG Index, _In_ FARPROC HookRoutine) {
  // Need to rise IRQL not to allow the system to change an execution processor
  // during the operation because this code changes a state of processor (CR0).
  KIRQL oldIrql = 0;
  KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
  const auto scopedIrql =
      stdexp::make_scope_exit([oldIrql]() { KeLowerIrql(oldIrql); });

  UtilDisableWriteProtect();
  const auto scopedWriteProtection =
      stdexp::make_scope_exit([] { UtilEnableWriteProtect(); });
  g_SSDTpTable->ServiceTable[Index] = reinterpret_cast<ULONG>(HookRoutine);
}

ALLOC_TEXT(INIT, SSDTpFindTable)
EXTERN_C static SERVICE_DESCRIPTOR_TABLE *SSDTpFindTable() {
  PAGED_CODE();

  if (!IsX64()) {
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"KeServiceDescriptorTable");
    return reinterpret_cast<SERVICE_DESCRIPTOR_TABLE *>(
        MmGetSystemRoutineAddress(&name));
  }

  UNICODE_STRING name = RTL_CONSTANT_STRING(L"KeAddSystemServiceTable");
  auto pKeAddSystemServiceTable =
      reinterpret_cast<UCHAR *>(MmGetSystemRoutineAddress(&name));
  if (!pKeAddSystemServiceTable) {
    return nullptr;
  }

  UNICODE_STRING name2 = RTL_CONSTANT_STRING(L"RtlPcToFileHeader");
  auto pRtlPcToFileHeader = reinterpret_cast<decltype(&RtlPcToFileHeader)>(
      MmGetSystemRoutineAddress(&name2));
  if (!pRtlPcToFileHeader) {
    return nullptr;
  }

  ULONG offset = 0;
  for (auto i = 0; i < 0x40; ++i) {
    auto dwordBytes = *reinterpret_cast<ULONG *>(pKeAddSystemServiceTable + i);
    if ((dwordBytes & 0x00fffff0) == 0x00bc8340) {
      offset = *reinterpret_cast<ULONG *>(pKeAddSystemServiceTable + i + 4);
      break;
    }
  }
  if (!offset) {
    return nullptr;
  }

  UCHAR *base = nullptr;
  if (!pRtlPcToFileHeader(pKeAddSystemServiceTable,
                          reinterpret_cast<void **>(&base))) {
    return nullptr;
  }
  return reinterpret_cast<SERVICE_DESCRIPTOR_TABLE *>(base + offset);
}