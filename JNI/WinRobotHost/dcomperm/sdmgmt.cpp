/*++

DCOM Permission Configuration Sample
Copyright (c) 1996, Microsoft Corporation. All rights reserved.

Module Name:

    sdmgmt.cpp

Abstract:

    Routines to manage security descriptors

Author:

    Michael Nelson

Environment:

    Windows NT

--*/

#include <windows.h>
#include <comdef.h>
#include <stdio.h>
#include <conio.h>
#include <malloc.h>
#include <tchar.h>
#include "ntsecapi.h"
#include "dcomperm.h"

DWORD
CreateNewSD (
    SECURITY_DESCRIPTOR **SD,
    bool install_defaults
    )
{
    PACL    dacl = NULL;
    DWORD   sidLength = 0;
    PSID    groupSID = NULL;
    PSID    ownerSID = NULL;
    DWORD   returnValue = 0;

    *SD = NULL;

    PSID    sid = NULL;
    returnValue = GetCurrentUserSID (&sid);
    if (returnValue != ERROR_SUCCESS)
        return returnValue;

    sidLength = GetLengthSid (sid);

    *SD = (SECURITY_DESCRIPTOR *) malloc (
        (sizeof (ACL)+sizeof (ACCESS_ALLOWED_ACE)+sidLength) +
        (2 * sidLength) +
        sizeof (SECURITY_DESCRIPTOR));

    groupSID = (SID *) (*SD + 1);
    ownerSID = (SID *) (((BYTE *) groupSID) + sidLength);
    dacl = (ACL *) (((BYTE *) ownerSID) + sidLength);

    if (!InitializeSecurityDescriptor (*SD, SECURITY_DESCRIPTOR_REVISION))
    {
        free (*SD);
        free (sid);
        return GetLastError();
    }

    if (!InitializeAcl (dacl,
                        sizeof (ACL)+sizeof (ACCESS_ALLOWED_ACE)+sidLength,
                        ACL_REVISION2))
    {
        free (*SD);
        free (sid);
        return GetLastError();
    }

    if (install_defaults) {
      if (!AddAccessAllowedAce (dacl,
                                ACL_REVISION2,
                                COM_RIGHTS_EXECUTE,
                                sid))
      {
          free (*SD);
          free (sid);
          return GetLastError();
      }
    }

    if (!SetSecurityDescriptorDacl (*SD, TRUE, dacl, FALSE))
    {
        free (*SD);
        free (sid);
        return GetLastError();
    }

    memcpy (groupSID, sid, sidLength);
    if (!SetSecurityDescriptorGroup (*SD, groupSID, FALSE))
    {
        free (*SD);
        free (sid);
        return GetLastError();
    }

    memcpy (ownerSID, sid, sidLength);
    if (!SetSecurityDescriptorOwner (*SD, ownerSID, FALSE))
    {
        free (*SD);
        free (sid);
        return GetLastError();
    }
    return ERROR_SUCCESS;
}


DWORD
MakeSDAbsolute (
    PSECURITY_DESCRIPTOR OldSD,
    PSECURITY_DESCRIPTOR *NewSD
    )
{
    PSECURITY_DESCRIPTOR  sd = NULL;
    DWORD                 descriptorSize = 0;
    DWORD                 daclSize = 0;
    DWORD                 saclSize = 0;
    DWORD                 ownerSIDSize = 0;
    DWORD                 groupSIDSize = 0;
    PACL                  dacl = NULL;
    PACL                  sacl = NULL;
    PSID                  ownerSID = NULL;
    PSID                  groupSID = NULL;
    BOOL                  present = false;
    BOOL                  systemDefault = false;

    //
    // Get SACL
    //

    if (!GetSecurityDescriptorSacl (OldSD, &present, &sacl, &systemDefault))
        return GetLastError();

    if (sacl && present)
    {
        saclSize = sacl->AclSize;
    } else saclSize = 0;

    //
    // Get DACL
    //

    if (!GetSecurityDescriptorDacl (OldSD, &present, &dacl, &systemDefault))
        return GetLastError();

    if (dacl && present)
    {
        daclSize = dacl->AclSize;
    } else daclSize = 0;

    //
    // Get Owner
    //

    if (!GetSecurityDescriptorOwner (OldSD, &ownerSID, &systemDefault))
        return GetLastError();

    ownerSIDSize = GetLengthSid (ownerSID);

    //
    // Get Group
    //

    if (!GetSecurityDescriptorGroup (OldSD, &groupSID, &systemDefault))
        return GetLastError();

    groupSIDSize = GetLengthSid (groupSID);

    //
    // Do the conversion
    //

    descriptorSize = 0;

    MakeAbsoluteSD (OldSD, sd, &descriptorSize, dacl, &daclSize, sacl,
                    &saclSize, ownerSID, &ownerSIDSize, groupSID,
                    &groupSIDSize);

    sd = (PSECURITY_DESCRIPTOR) malloc( SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!InitializeSecurityDescriptor (sd, SECURITY_DESCRIPTOR_REVISION))
        return GetLastError();

    if (!MakeAbsoluteSD (OldSD, sd, &descriptorSize, dacl, &daclSize, sacl,
                         &saclSize, ownerSID, &ownerSIDSize, groupSID,
                         &groupSIDSize))
        return GetLastError();

    *NewSD = sd;
    return ERROR_SUCCESS;
}

DWORD
SetNamedValueSD (
    HKEY RootKey,
    LPTSTR KeyName,
    LPTSTR ValueName,
    SECURITY_DESCRIPTOR *SD
    )
{
    DWORD   returnValue = 0;
    DWORD   disposition = 0;
    HKEY    registryKey;

    //
    // Create new key or open existing key
    //

    returnValue = RegCreateKeyEx (RootKey, KeyName, 0, TEXT(""), 0, KEY_ALL_ACCESS, NULL, &registryKey, &disposition);
    if (returnValue != ERROR_SUCCESS)
        return returnValue;

    //
    // Write the security descriptor
    //

    returnValue = RegSetValueEx (registryKey, ValueName, 0, REG_BINARY, (LPBYTE) SD, GetSecurityDescriptorLength (SD));
    if (returnValue != ERROR_SUCCESS)
        return returnValue;

    RegCloseKey (registryKey);

    return ERROR_SUCCESS;
}

DWORD
GetNamedValueSD (
    HKEY RootKey,
    LPTSTR KeyName,
    LPTSTR ValueName,
    SECURITY_DESCRIPTOR **SD,
    BOOL *NewSD,
    bool install_defaults
    )
{
    DWORD               returnValue = 0;
    HKEY                registryKey;
    DWORD               valueType = 0;
    DWORD               valueSize = 0;

    *NewSD = FALSE;

    //
    // Get the security descriptor from the named value. If it doesn't
    // exist, create a fresh one.
    //

    returnValue = RegOpenKeyEx (RootKey, KeyName, 0, KEY_ALL_ACCESS, &registryKey);

    if (returnValue != ERROR_SUCCESS)
    {
        if (returnValue == ERROR_FILE_NOT_FOUND)
        {
            *SD = NULL;
            returnValue = CreateNewSD (SD, install_defaults);
            if (returnValue != ERROR_SUCCESS)
                return returnValue;

            *NewSD = TRUE;
            return ERROR_SUCCESS;
        } else
            return returnValue;
    }

    returnValue = RegQueryValueEx (registryKey, ValueName, NULL, &valueType, NULL, &valueSize);

    if (returnValue && returnValue != ERROR_INSUFFICIENT_BUFFER)
    {
        *SD = NULL;
        returnValue = CreateNewSD (SD, install_defaults);
        if (returnValue != ERROR_SUCCESS)
            return returnValue;

        *NewSD = TRUE;
    } else
    {
        *SD = (SECURITY_DESCRIPTOR *) malloc (valueSize);

        returnValue = RegQueryValueEx (registryKey, ValueName, NULL, &valueType, (LPBYTE) *SD, &valueSize);
        if (returnValue)
        {
            free (*SD);

            *SD = NULL;
            returnValue = CreateNewSD (SD, install_defaults);
            if (returnValue != ERROR_SUCCESS)
                return returnValue;

            *NewSD = TRUE;
        }
    }

    RegCloseKey (registryKey);

    return ERROR_SUCCESS;
}

DWORD
ListNamedValueSD (
    HKEY RootKey,
    LPTSTR KeyName,
    LPTSTR ValueName
    )
{
    DWORD               returnValue = 0;
    SECURITY_DESCRIPTOR *sd = NULL;
    BOOL                present = false;
    BOOL                defaultDACL = false;
    PACL                dacl = NULL;
    BOOL                newSD = FALSE;

    returnValue = GetNamedValueSD (RootKey, KeyName, ValueName, &sd, &newSD);

    if ((returnValue != ERROR_SUCCESS) || (newSD == TRUE))
    {
        _tprintf (TEXT("<Using Default Permissions>\n"));
        free (sd);
        return returnValue;
    }

    if (!GetSecurityDescriptorDacl (sd, &present, &dacl, &defaultDACL))
    {
        free (sd);
        return GetLastError();
    }

    if (!present)
    {
        _tprintf (TEXT("<Access is denied to everyone>\n"));
        free (sd);
        return ERROR_SUCCESS;
    }

    ListACL (dacl);

    free (sd);

    return ERROR_SUCCESS;
}

DWORD ZapNamedValueSD(HKEY RootKey, LPTSTR KeyName, LPTSTR ValueName)
{
    DWORD               returnValue = 0;
    SECURITY_DESCRIPTOR *sd = NULL;
    BOOL                present = true;
    BOOL                defaultDACL = false;
    PACL                dacl = NULL;
    BOOL                newSD = FALSE;
    HKEY registryKey;

    // first whack the launch permissions.
    returnValue = RegOpenKeyEx (HKEY_CLASSES_ROOT, KeyName, 0, KEY_ALL_ACCESS, &registryKey);
    if (returnValue != ERROR_SUCCESS && returnValue != ERROR_FILE_NOT_FOUND) {
      _tprintf (TEXT("ERROR: Cannot open AppID registry key.\n"));
      return returnValue;
    }

    returnValue = RegDeleteValue (registryKey, TEXT("LaunchPermission"));
    if (returnValue != ERROR_SUCCESS && returnValue != ERROR_FILE_NOT_FOUND) {
      _tprintf (TEXT("ERROR: Cannot delete LaunchPermission value.\n"));
      return returnValue;
    }

    RegCloseKey (registryKey);

    returnValue = AddPrincipalToNamedValueSD(RootKey, KeyName, ValueName,
        _T("Everyone"), false, false);
    if (returnValue != ERROR_SUCCESS) {
      _tprintf (TEXT("ERROR: Cannot deny Everyone.\n"));
      return returnValue;
    }

    return ERROR_SUCCESS;
}

DWORD
AddPrincipalToNamedValueSD (
    HKEY RootKey,
    LPTSTR KeyName,
    LPTSTR ValueName,
    LPTSTR Principal,
    BOOL Permit,
    bool install_defaults,
	DWORD PermissionMask
    )
{
    DWORD               returnValue = 0;
    SECURITY_DESCRIPTOR *sd = NULL;
    SECURITY_DESCRIPTOR *sdSelfRelative = NULL;
    SECURITY_DESCRIPTOR *sdAbsolute = NULL;
    DWORD               secDescSize = 0;
    BOOL                present = false;
    BOOL                defaultDACL = false;
    PACL                dacl = NULL;
    BOOL                newSD = FALSE;

    returnValue = GetNamedValueSD (RootKey, KeyName, ValueName, &sd, &newSD, install_defaults);

    //
    // Get security descriptor from registry or create a new one
    //

    if (returnValue != ERROR_SUCCESS)
        return returnValue;

    if (!GetSecurityDescriptorDacl (sd, &present, &dacl, &defaultDACL))
        return GetLastError();

    if (newSD && install_defaults)
    {
        AddAccessAllowedACEToACL (&dacl, COM_RIGHTS_EXECUTE, TEXT("SYSTEM"));
        AddAccessAllowedACEToACL (&dacl, COM_RIGHTS_EXECUTE, TEXT("INTERACTIVE"));
    }

    //
    // Add the Principal that the caller wants added
    //

    if (Permit)
        returnValue = AddAccessAllowedACEToACL (&dacl, PermissionMask, Principal);
	else
        returnValue = AddAccessDeniedACEToACL (&dacl, GENERIC_ALL, Principal);

    if (returnValue != ERROR_SUCCESS)
    {
        free (sd);
        return returnValue;
    }

    //
    // Make the security descriptor absolute if it isn't new
    //

    if (!newSD)
        MakeSDAbsolute ((PSECURITY_DESCRIPTOR) sd, (PSECURITY_DESCRIPTOR *) &sdAbsolute); else
        sdAbsolute = sd;

    //
    // Set the discretionary ACL on the security descriptor
    //

    if (!SetSecurityDescriptorDacl (sdAbsolute, TRUE, dacl, FALSE))
        return GetLastError();

    //
    // Make the security descriptor self-relative so that we can
    // store it in the registry
    //

    secDescSize = 0;
    MakeSelfRelativeSD (sdAbsolute, sdSelfRelative, &secDescSize);
    sdSelfRelative = (SECURITY_DESCRIPTOR *) malloc (secDescSize);
    if (!MakeSelfRelativeSD (sdAbsolute, sdSelfRelative, &secDescSize))
        return GetLastError();

    //
    // Store the security descriptor in the registry
    //

    SetNamedValueSD (RootKey, KeyName, ValueName, sdSelfRelative);

    free (sd);
    free (sdSelfRelative);
    free (sdAbsolute);

    return ERROR_SUCCESS;
}

DWORD
RemovePrincipalFromNamedValueSD (
    HKEY RootKey,
    LPTSTR KeyName,
    LPTSTR ValueName,
    LPTSTR Principal,
    bool install_defaults
    )
{
    DWORD               returnValue = 0;
    SECURITY_DESCRIPTOR *sd = NULL;
    SECURITY_DESCRIPTOR *sdSelfRelative = NULL;
    SECURITY_DESCRIPTOR *sdAbsolute = NULL;
    DWORD               secDescSize = 0;
    BOOL                present = false;
    BOOL                defaultDACL = false;
    PACL                dacl = NULL;
    BOOL                newSD = FALSE;

    returnValue = GetNamedValueSD (RootKey, KeyName, ValueName, &sd, &newSD);

    //
    // Get security descriptor from registry or create a new one
    //

    if (returnValue != ERROR_SUCCESS)
        return returnValue;

    if (!GetSecurityDescriptorDacl (sd, &present, &dacl, &defaultDACL))
        return GetLastError();

    //
    // If the security descriptor is new, add the required Principals to it
    //

    if (newSD && install_defaults)
    {
        AddAccessAllowedACEToACL (&dacl, COM_RIGHTS_EXECUTE, TEXT("SYSTEM"));
        AddAccessAllowedACEToACL (&dacl, COM_RIGHTS_EXECUTE, TEXT("INTERACTIVE"));
    }

    //
    // Remove the Principal that the caller wants removed
    //

    returnValue = RemovePrincipalFromACL (dacl, Principal);
    if (returnValue != ERROR_SUCCESS)
    {
        free (sd);
        return returnValue;
    }

    //
    // Make the security descriptor absolute if it isn't new
    //

    if (!newSD)
        MakeSDAbsolute ((PSECURITY_DESCRIPTOR) sd, (PSECURITY_DESCRIPTOR *) &sdAbsolute); 
	else
        sdAbsolute = sd;

    //
    // Set the discretionary ACL on the security descriptor
    //

    if (!SetSecurityDescriptorDacl (sdAbsolute, TRUE, dacl, FALSE))
        return GetLastError();

    //
    // Make the security descriptor self-relative so that we can
    // store it in the registry
    //

    secDescSize = 0;
    MakeSelfRelativeSD (sdAbsolute, sdSelfRelative, &secDescSize);
    sdSelfRelative = (SECURITY_DESCRIPTOR *) malloc (secDescSize);
    if (!MakeSelfRelativeSD (sdAbsolute, sdSelfRelative, &secDescSize))
        return GetLastError();

    //
    // Store the security descriptor in the registry
    //

    SetNamedValueSD (RootKey, KeyName, ValueName, sdSelfRelative);

    free (sd);
    free (sdSelfRelative);
    if(sd != sdAbsolute)free (sdAbsolute);

    return ERROR_SUCCESS;
}

