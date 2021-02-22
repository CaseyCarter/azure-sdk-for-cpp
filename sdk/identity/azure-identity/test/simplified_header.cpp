// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/**
 * @brief makes sure azure/identity.hpp can be included.
 *
 * @remark This file will catch any issue while trying to use/include the identity.hpp header
 *
 */

// Verify that including windows.h, which defines "min" and "max" as macros, does not fail any code
// in SDK headers.
#if defined(_MSC_VER)
#include <windows.h>
#endif // _MSC_VER

#include <azure/identity.hpp>
#include <gtest/gtest.h>

class DllExportTest {
  AZ_IDENTITY_DLLEXPORT static const bool DllExportHIncluded;
};

TEST(SimplifiedHeader, identity)
{
  EXPECT_NO_THROW(Azure::Identity::ClientSecretCredential clientSecretCredential("", "", ""));
  EXPECT_NO_THROW(Azure::Identity::EnvironmentCredential environmentCredential);
  EXPECT_NO_THROW(Azure::Identity::Details::Version::VersionString());
}
