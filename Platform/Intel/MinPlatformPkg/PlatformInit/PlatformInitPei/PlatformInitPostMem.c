/** @file
  Source code file for Platform Init PEI module

Copyright (c) 2017 - 2019, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/IoLib.h>
#include <Library/HobLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PeiServicesLib.h>
#include <IndustryStandard/Pci30.h>
#include <Ppi/EndOfPeiPhase.h>

#include <Guid/FirmwareFileSystem2.h>
#include <Protocol/FirmwareVolumeBlock.h>

#include <Library/TimerLib.h>
#include <Library/BoardInitLib.h>
#include <Library/TestPointCheckLib.h>
#include <Library/SetCacheMtrrLib.h>
#include <Library/SmmRelocationLib.h>
#include <Ppi/MpServices2.h>
#include <Guid/SmmBaseHob.h>

EFI_STATUS
EFIAPI
PlatformInitEndOfPei (
  IN CONST EFI_PEI_SERVICES     **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR  *NotifyDescriptor,
  IN VOID                       *Ppi
  );

static EFI_PEI_NOTIFY_DESCRIPTOR  mEndOfPeiNotifyList = {
  (EFI_PEI_PPI_DESCRIPTOR_NOTIFY_CALLBACK | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEfiEndOfPeiSignalPpiGuid,
  (EFI_PEIM_NOTIFY_ENTRY_POINT) PlatformInitEndOfPei
};

//
// SmmRelocationInit requires gEfiSmmSmramMemoryGuid to be in the HOB list.
// For FSP-wrapper builds this HOB is only transferred from the FSP HOB heap
// into the UEFI HOB list during PostFspsHobProcess(), which runs at the end
// of FspsWrapperPeim.  That PEIM installs gEdkiiSiliconInitializedPpiGuid
// after ProcessFspHobList() completes, so by the time this callback fires
// the SMRAM HOB is guaranteed to be present and MpServices2 is already up.
//
STATIC
EFI_STATUS
EFIAPI
OnSiliconInitialized (
  IN EFI_PEI_SERVICES           **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR  *NotifyDescriptor,
  IN VOID                       *Ppi
  )
{
  EFI_STATUS                Status;
  EFI_PEI_MP_SERVICES2_PPI  *MpServices2;

  Status = PeiServicesLocatePpi (
             &gEfiPeiMpServices2PpiGuid,
             0,
             NULL,
             (VOID **)&MpServices2
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "OnSiliconInitialized: MpServices2 not found: %r\n", Status));
    return EFI_SUCCESS;
  }

  Status = SmmRelocationInit (MpServices2);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "OnSiliconInitialized: SmmRelocationInit failed: %r\n", Status));
  }

  return EFI_SUCCESS;
}

STATIC CONST EFI_PEI_NOTIFY_DESCRIPTOR  mSiliconInitializedSmmRelocationNotify = {
  EFI_PEI_PPI_DESCRIPTOR_NOTIFY_CALLBACK |
  EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
  &gEdkiiSiliconInitializedPpiGuid,
  OnSiliconInitialized
};

/**
  This function handles PlatformInit task at the end of PEI

  @param[in]  PeiServices  Pointer to PEI Services Table.
  @param[in]  NotifyDesc   Pointer to the descriptor for the Notification event that
                           caused this function to execute.
  @param[in]  Ppi          Pointer to the PPI data associated with this function.

  @retval     EFI_SUCCESS  The function completes successfully
  @retval     others
**/
EFI_STATUS
EFIAPI
PlatformInitEndOfPei (
  IN CONST EFI_PEI_SERVICES     **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR  *NotifyDescriptor,
  IN VOID                       *Ppi
  )
{
  EFI_STATUS                    Status;

  Status = BoardInitAfterSiliconInit ();
  ASSERT_EFI_ERROR (Status);

  TestPointEndOfPeiSystemResourceFunctional ();

  TestPointEndOfPeiPciBusMasterDisabled ();

  Status = SetCacheMtrrAfterEndOfPei ();
  ASSERT_EFI_ERROR (Status);

  TestPointEndOfPeiMtrrFunctional ();

  return Status;
}


/**
  Platform Init PEI module entry point

  @param[in]  FileHandle           Not used.
  @param[in]  PeiServices          General purpose services available to every PEIM.

  @retval     EFI_SUCCESS          The function completes successfully
  @retval     EFI_OUT_OF_RESOURCES Insufficient resources to create database
**/
EFI_STATUS
EFIAPI
PlatformInitPostMemEntryPoint (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  EFI_STATUS                       Status;

  Status = BoardInitBeforeSiliconInit ();
  ASSERT_EFI_ERROR (Status);

  //
  // Performing PlatformInitEndOfPei after EndOfPei PPI produced
  //
  Status = PeiServicesNotifyPpi (&mEndOfPeiNotifyList);

  //
  // Register for SMM base relocation after silicon init completes.
  // gEdkiiSiliconInitializedPpiGuid is installed at the end of
  // PostFspsHobProcess(), which runs ProcessFspHobList() first --
  // that is the step that copies gEfiSmmSmramMemoryGuid from the FSP
  // HOB heap into the UEFI HOB list.  SmmRelocationInit() requires
  // that HOB to be present, so we must not attempt relocation earlier
  // (e.g. on gEfiPeiMpServices2PpiGuid, which fires before FSP-S).
  //
  if (GetFirstGuidHob (&gSmmBaseHobGuid) == NULL) {
    PeiServicesNotifyPpi (&mSiliconInitializedSmmRelocationNotify);
  }

  return Status;
}
