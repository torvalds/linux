// <copyright file="Program.cpp" company="Microsoft Corporation">
// Copyright (C) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.
// </copyright>
// <license>
// The MIT License (MIT)
//
// Copyright (C) Microsoft Corporation. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// </license>

#pragma once

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif

// Constants
//
#ifndef E_NOTFOUND
#define E_NOTFOUND HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
#endif

#ifndef E_FILENOTFOUND
#define E_FILENOTFOUND HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
#endif

// Enumerations
//
/// <summary>
/// The state of an instance.
/// </summary>
enum InstanceState : unsigned {
  /// <summary>
  /// The instance state has not been determined.
  /// </summary>
  eNone = 0,

  /// <summary>
  /// The instance installation path exists.
  /// </summary>
  eLocal = 1,

  /// <summary>
  /// A product is registered to the instance.
  /// </summary>
  eRegistered = 2,

  /// <summary>
  /// No reboot is required for the instance.
  /// </summary>
  eNoRebootRequired = 4,

  /// <summary>
  /// The instance represents a complete install.
  /// </summary>
  eComplete = MAXUINT,
};

// Forward interface declarations
//
#ifndef __ISetupInstance_FWD_DEFINED__
#define __ISetupInstance_FWD_DEFINED__
typedef struct ISetupInstance ISetupInstance;
#endif

#ifndef __ISetupInstance2_FWD_DEFINED__
#define __ISetupInstance2_FWD_DEFINED__
typedef struct ISetupInstance2 ISetupInstance2;
#endif

#ifndef __IEnumSetupInstances_FWD_DEFINED__
#define __IEnumSetupInstances_FWD_DEFINED__
typedef struct IEnumSetupInstances IEnumSetupInstances;
#endif

#ifndef __ISetupConfiguration_FWD_DEFINED__
#define __ISetupConfiguration_FWD_DEFINED__
typedef struct ISetupConfiguration ISetupConfiguration;
#endif

#ifndef __ISetupConfiguration2_FWD_DEFINED__
#define __ISetupConfiguration2_FWD_DEFINED__
typedef struct ISetupConfiguration2 ISetupConfiguration2;
#endif

#ifndef __ISetupPackageReference_FWD_DEFINED__
#define __ISetupPackageReference_FWD_DEFINED__
typedef struct ISetupPackageReference ISetupPackageReference;
#endif

#ifndef __ISetupHelper_FWD_DEFINED__
#define __ISetupHelper_FWD_DEFINED__
typedef struct ISetupHelper ISetupHelper;
#endif

// Forward class declarations
//
#ifndef __SetupConfiguration_FWD_DEFINED__
#define __SetupConfiguration_FWD_DEFINED__

#ifdef __cplusplus
typedef class SetupConfiguration SetupConfiguration;
#endif

#endif

#ifdef __cplusplus
extern "C" {
#endif

// Interface definitions
//
EXTERN_C const IID IID_ISetupInstance;

#if defined(__cplusplus) && !defined(CINTERFACE)
/// <summary>
/// Information about an instance of a product.
/// </summary>
struct DECLSPEC_UUID("B41463C3-8866-43B5-BC33-2B0676F7F42E")
    DECLSPEC_NOVTABLE ISetupInstance : public IUnknown {
  /// <summary>
  /// Gets the instance identifier (should match the name of the parent instance
  /// directory).
  /// </summary>
  /// <param name="pbstrInstanceId">The instance identifier.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist.</returns>
  STDMETHOD(GetInstanceId)(_Out_ BSTR *pbstrInstanceId) = 0;

  /// <summary>
  /// Gets the local date and time when the installation was originally
  /// installed.
  /// </summary>
  /// <param name="pInstallDate">The local date and time when the installation
  /// was originally installed.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist and E_NOTFOUND if the
  /// property is not defined.</returns>
  STDMETHOD(GetInstallDate)(_Out_ LPFILETIME pInstallDate) = 0;

  /// <summary>
  /// Gets the unique name of the installation, often indicating the branch and
  /// other information used for telemetry.
  /// </summary>
  /// <param name="pbstrInstallationName">The unique name of the installation,
  /// often indicating the branch and other information used for
  /// telemetry.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist and E_NOTFOUND if the
  /// property is not defined.</returns>
  STDMETHOD(GetInstallationName)(_Out_ BSTR *pbstrInstallationName) = 0;

  /// <summary>
  /// Gets the path to the installation root of the product.
  /// </summary>
  /// <param name="pbstrInstallationPath">The path to the installation root of
  /// the product.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist and E_NOTFOUND if the
  /// property is not defined.</returns>
  STDMETHOD(GetInstallationPath)(_Out_ BSTR *pbstrInstallationPath) = 0;

  /// <summary>
  /// Gets the version of the product installed in this instance.
  /// </summary>
  /// <param name="pbstrInstallationVersion">The version of the product
  /// installed in this instance.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist and E_NOTFOUND if the
  /// property is not defined.</returns>
  STDMETHOD(GetInstallationVersion)(_Out_ BSTR *pbstrInstallationVersion) = 0;

  /// <summary>
  /// Gets the display name (title) of the product installed in this instance.
  /// </summary>
  /// <param name="lcid">The LCID for the display name.</param>
  /// <param name="pbstrDisplayName">The display name (title) of the product
  /// installed in this instance.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist and E_NOTFOUND if the
  /// property is not defined.</returns>
  STDMETHOD(GetDisplayName)(_In_ LCID lcid, _Out_ BSTR *pbstrDisplayName) = 0;

  /// <summary>
  /// Gets the description of the product installed in this instance.
  /// </summary>
  /// <param name="lcid">The LCID for the description.</param>
  /// <param name="pbstrDescription">The description of the product installed in
  /// this instance.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist and E_NOTFOUND if the
  /// property is not defined.</returns>
  STDMETHOD(GetDescription)(_In_ LCID lcid, _Out_ BSTR *pbstrDescription) = 0;

  /// <summary>
  /// Resolves the optional relative path to the root path of the instance.
  /// </summary>
  /// <param name="pwszRelativePath">A relative path within the instance to
  /// resolve, or NULL to get the root path.</param>
  /// <param name="pbstrAbsolutePath">The full path to the optional relative
  /// path within the instance. If the relative path is NULL, the root path will
  /// always terminate in a backslash.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist and E_NOTFOUND if the
  /// property is not defined.</returns>
  STDMETHOD(ResolvePath)
  (_In_opt_z_ LPCOLESTR pwszRelativePath, _Out_ BSTR *pbstrAbsolutePath) = 0;
};
#endif

EXTERN_C const IID IID_ISetupInstance2;

#if defined(__cplusplus) && !defined(CINTERFACE)
/// <summary>
/// Information about an instance of a product.
/// </summary>
struct DECLSPEC_UUID("89143C9A-05AF-49B0-B717-72E218A2185C")
    DECLSPEC_NOVTABLE ISetupInstance2 : public ISetupInstance {
  /// <summary>
  /// Gets the state of the instance.
  /// </summary>
  /// <param name="pState">The state of the instance.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist.</returns>
  STDMETHOD(GetState)(_Out_ InstanceState *pState) = 0;

  /// <summary>
  /// Gets an array of package references registered to the instance.
  /// </summary>
  /// <param name="ppsaPackages">Pointer to an array of <see
  /// cref="ISetupPackageReference"/>.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist and E_NOTFOUND if the
  /// packages property is not defined.</returns>
  STDMETHOD(GetPackages)(_Out_ LPSAFEARRAY *ppsaPackages) = 0;

  /// <summary>
  /// Gets a pointer to the <see cref="ISetupPackageReference"/> that represents
  /// the registered product.
  /// </summary>
  /// <param name="ppPackage">Pointer to an instance of <see
  /// cref="ISetupPackageReference"/>. This may be NULL if <see
  /// cref="GetState"/> does not return <see cref="eComplete"/>.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist and E_NOTFOUND if the
  /// packages property is not defined.</returns>
  STDMETHOD(GetProduct)
  (_Outptr_result_maybenull_ ISetupPackageReference **ppPackage) = 0;

  /// <summary>
  /// Gets the relative path to the product application, if available.
  /// </summary>
  /// <param name="pbstrProductPath">The relative path to the product
  /// application, if available.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_FILENOTFOUND if the instance state does not exist.</returns>
  STDMETHOD(GetProductPath)
  (_Outptr_result_maybenull_ BSTR *pbstrProductPath) = 0;
};
#endif

EXTERN_C const IID IID_IEnumSetupInstances;

#if defined(__cplusplus) && !defined(CINTERFACE)
/// <summary>
/// A enumerator of installed <see cref="ISetupInstance"/> objects.
/// </summary>
struct DECLSPEC_UUID("6380BCFF-41D3-4B2E-8B2E-BF8A6810C848")
    DECLSPEC_NOVTABLE IEnumSetupInstances : public IUnknown {
  /// <summary>
  /// Retrieves the next set of product instances in the enumeration sequence.
  /// </summary>
  /// <param name="celt">The number of product instances to retrieve.</param>
  /// <param name="rgelt">A pointer to an array of <see
  /// cref="ISetupInstance"/>.</param>
  /// <param name="pceltFetched">A pointer to the number of product instances
  /// retrieved. If celt is 1 this parameter may be NULL.</param>
  /// <returns>S_OK if the number of elements were fetched, S_FALSE if nothing
  /// was fetched (at end of enumeration), E_INVALIDARG if celt is greater than
  /// 1 and pceltFetched is NULL, or E_OUTOFMEMORY if an <see
  /// cref="ISetupInstance"/> could not be allocated.</returns>
  STDMETHOD(Next)
  (_In_ ULONG celt, _Out_writes_to_(celt, *pceltFetched) ISetupInstance **rgelt,
   _Out_opt_ _Deref_out_range_(0, celt) ULONG *pceltFetched) = 0;

  /// <summary>
  /// Skips the next set of product instances in the enumeration sequence.
  /// </summary>
  /// <param name="celt">The number of product instances to skip.</param>
  /// <returns>S_OK if the number of elements could be skipped; otherwise,
  /// S_FALSE;</returns>
  STDMETHOD(Skip)(_In_ ULONG celt) = 0;

  /// <summary>
  /// Resets the enumeration sequence to the beginning.
  /// </summary>
  /// <returns>Always returns S_OK;</returns>
  STDMETHOD(Reset)(void) = 0;

  /// <summary>
  /// Creates a new enumeration object in the same state as the current
  /// enumeration object: the new object points to the same place in the
  /// enumeration sequence.
  /// </summary>
  /// <param name="ppenum">A pointer to a pointer to a new <see
  /// cref="IEnumSetupInstances"/> interface. If the method fails, this
  /// parameter is undefined.</param>
  /// <returns>S_OK if a clone was returned; otherwise, E_OUTOFMEMORY.</returns>
  STDMETHOD(Clone)(_Deref_out_opt_ IEnumSetupInstances **ppenum) = 0;
};
#endif

EXTERN_C const IID IID_ISetupConfiguration;

#if defined(__cplusplus) && !defined(CINTERFACE)
/// <summary>
/// Gets information about product instances set up on the machine.
/// </summary>
struct DECLSPEC_UUID("42843719-DB4C-46C2-8E7C-64F1816EFD5B")
    DECLSPEC_NOVTABLE ISetupConfiguration : public IUnknown {
  /// <summary>
  /// Enumerates all completed product instances installed.
  /// </summary>
  /// <param name="ppEnumInstances">An enumeration of completed, installed
  /// product instances.</param>
  /// <returns>Standard HRESULT indicating success or failure.</returns>
  STDMETHOD(EnumInstances)(_Out_ IEnumSetupInstances **ppEnumInstances) = 0;

  /// <summary>
  /// Gets the instance for the current process path.
  /// </summary>
  /// <param name="ppInstance">The instance for the current process
  /// path.</param>
  /// <returns>The instance for the current process path, or E_NOTFOUND if not
  /// found.</returns>
  STDMETHOD(GetInstanceForCurrentProcess)
  (_Out_ ISetupInstance **ppInstance) = 0;

  /// <summary>
  /// Gets the instance for the given path.
  /// </summary>
  /// <param name="ppInstance">The instance for the given path.</param>
  /// <returns>The instance for the given path, or E_NOTFOUND if not
  /// found.</returns>
  STDMETHOD(GetInstanceForPath)
  (_In_z_ LPCWSTR wzPath, _Out_ ISetupInstance **ppInstance) = 0;
};
#endif

EXTERN_C const IID IID_ISetupConfiguration2;

#if defined(__cplusplus) && !defined(CINTERFACE)
/// <summary>
/// Gets information about product instances.
/// </summary>
struct DECLSPEC_UUID("26AAB78C-4A60-49D6-AF3B-3C35BC93365D")
    DECLSPEC_NOVTABLE ISetupConfiguration2 : public ISetupConfiguration {
  /// <summary>
  /// Enumerates all product instances.
  /// </summary>
  /// <param name="ppEnumInstances">An enumeration of all product
  /// instances.</param>
  /// <returns>Standard HRESULT indicating success or failure.</returns>
  STDMETHOD(EnumAllInstances)(_Out_ IEnumSetupInstances **ppEnumInstances) = 0;
};
#endif

EXTERN_C const IID IID_ISetupPackageReference;

#if defined(__cplusplus) && !defined(CINTERFACE)
/// <summary>
/// A reference to a package.
/// </summary>
struct DECLSPEC_UUID("da8d8a16-b2b6-4487-a2f1-594ccccd6bf5")
    DECLSPEC_NOVTABLE ISetupPackageReference : public IUnknown {
  /// <summary>
  /// Gets the general package identifier.
  /// </summary>
  /// <param name="pbstrId">The general package identifier.</param>
  /// <returns>Standard HRESULT indicating success or failure.</returns>
  STDMETHOD(GetId)(_Out_ BSTR *pbstrId) = 0;

  /// <summary>
  /// Gets the version of the package.
  /// </summary>
  /// <param name="pbstrVersion">The version of the package.</param>
  /// <returns>Standard HRESULT indicating success or failure.</returns>
  STDMETHOD(GetVersion)(_Out_ BSTR *pbstrVersion) = 0;

  /// <summary>
  /// Gets the target process architecture of the package.
  /// </summary>
  /// <param name="pbstrChip">The target process architecture of the
  /// package.</param>
  /// <returns>Standard HRESULT indicating success or failure.</returns>
  STDMETHOD(GetChip)(_Out_ BSTR *pbstrChip) = 0;

  /// <summary>
  /// Gets the language and optional region identifier.
  /// </summary>
  /// <param name="pbstrLanguage">The language and optional region
  /// identifier.</param>
  /// <returns>Standard HRESULT indicating success or failure.</returns>
  STDMETHOD(GetLanguage)(_Out_ BSTR *pbstrLanguage) = 0;

  /// <summary>
  /// Gets the build branch of the package.
  /// </summary>
  /// <param name="pbstrBranch">The build branch of the package.</param>
  /// <returns>Standard HRESULT indicating success or failure.</returns>
  STDMETHOD(GetBranch)(_Out_ BSTR *pbstrBranch) = 0;

  /// <summary>
  /// Gets the type of the package.
  /// </summary>
  /// <param name="pbstrType">The type of the package.</param>
  /// <returns>Standard HRESULT indicating success or failure.</returns>
  STDMETHOD(GetType)(_Out_ BSTR *pbstrType) = 0;

  /// <summary>
  /// Gets the unique identifier consisting of all defined tokens.
  /// </summary>
  /// <param name="pbstrUniqueId">The unique identifier consisting of all
  /// defined tokens.</param>
  /// <returns>Standard HRESULT indicating success or failure, including
  /// E_UNEXPECTED if no Id was defined (required).</returns>
  STDMETHOD(GetUniqueId)(_Out_ BSTR *pbstrUniqueId) = 0;
};
#endif

EXTERN_C const IID IID_ISetupHelper;

#if defined(__cplusplus) && !defined(CINTERFACE)
/// <summary>
/// Helper functions.
/// </summary>
/// <remarks>
/// You can query for this interface from the <see cref="SetupConfiguration"/>
/// class.
/// </remarks>
struct DECLSPEC_UUID("42b21b78-6192-463e-87bf-d577838f1d5c")
    DECLSPEC_NOVTABLE ISetupHelper : public IUnknown {
  /// <summary>
  /// Parses a dotted quad version string into a 64-bit unsigned integer.
  /// </summary>
  /// <param name="pwszVersion">The dotted quad version string to parse, e.g.
  /// 1.2.3.4.</param>
  /// <param name="pullVersion">A 64-bit unsigned integer representing the
  /// version. You can compare this to other versions.</param>
  /// <returns>Standard HRESULT indicating success or failure.</returns>
  STDMETHOD(ParseVersion)
  (_In_ LPCOLESTR pwszVersion, _Out_ PULONGLONG pullVersion) = 0;

  /// <summary>
  /// Parses a dotted quad version string into a 64-bit unsigned integer.
  /// </summary>
  /// <param name="pwszVersionRange">The string containing 1 or 2 dotted quad
  /// version strings to parse, e.g. [1.0,) that means 1.0.0.0 or newer.</param>
  /// <param name="pullMinVersion">A 64-bit unsigned integer representing the
  /// minimum version, which may be 0. You can compare this to other
  /// versions.</param>
  /// <param name="pullMaxVersion">A 64-bit unsigned integer representing the
  /// maximum version, which may be MAXULONGLONG. You can compare this to other
  /// versions.</param>
  /// <returns>Standard HRESULT indicating success or failure.</returns>
  STDMETHOD(ParseVersionRange)
  (_In_ LPCOLESTR pwszVersionRange, _Out_ PULONGLONG pullMinVersion,
   _Out_ PULONGLONG pullMaxVersion) = 0;
};
#endif

// Class declarations
//
EXTERN_C const CLSID CLSID_SetupConfiguration;

#ifdef __cplusplus
/// <summary>
/// This class implements <see cref="ISetupConfiguration"/>, <see
/// cref="ISetupConfiguration2"/>, and <see cref="ISetupHelper"/>.
/// </summary>
class DECLSPEC_UUID("177F0C4A-1CD3-4DE7-A32C-71DBBB9FA36D") SetupConfiguration;
#endif

// Function declarations
//
/// <summary>
/// Gets an <see cref="ISetupConfiguration"/> that provides information about
/// product instances installed on the machine.
/// </summary>
/// <param name="ppConfiguration">The <see cref="ISetupConfiguration"/> that
/// provides information about product instances installed on the
/// machine.</param>
/// <param name="pReserved">Reserved for future use.</param>
/// <returns>Standard HRESULT indicating success or failure.</returns>
STDMETHODIMP GetSetupConfiguration(_Out_ ISetupConfiguration **ppConfiguration,
                                   _Reserved_ LPVOID pReserved);

#ifdef __cplusplus
}
#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif
