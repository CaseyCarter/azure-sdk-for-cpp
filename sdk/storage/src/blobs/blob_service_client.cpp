// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "blobs/blob_service_client.hpp"

#include "common/common_headers_request_policy.hpp"
#include "common/constants.hpp"
#include "common/shared_key_policy.hpp"
#include "common/storage_common.hpp"
#include "common/storage_version.hpp"
#include "credentials/policy/policies.hpp"
#include "http/curl/curl.hpp"

namespace Azure { namespace Storage { namespace Blobs {

  BlobServiceClient BlobServiceClient::CreateFromConnectionString(
      const std::string& connectionString,
      const BlobServiceClientOptions& options)
  {
    auto parsedConnectionString = Details::ParseConnectionString(connectionString);
    auto serviceUri = std::move(parsedConnectionString.BlobServiceUri);

    if (parsedConnectionString.KeyCredential)
    {
      return BlobServiceClient(
          serviceUri.ToString(), parsedConnectionString.KeyCredential, options);
    }
    else
    {
      return BlobServiceClient(serviceUri.ToString(), options);
    }
  }

  BlobServiceClient::BlobServiceClient(
      const std::string& serviceUri,
      std::shared_ptr<SharedKeyCredential> credential,
      const BlobServiceClientOptions& options)
      : m_serviceUrl(serviceUri)
  {
    std::vector<std::unique_ptr<Azure::Core::Http::HttpPolicy>> policies;
    policies.emplace_back(std::make_unique<Azure::Core::Http::TelemetryPolicy>(
        Details::c_BlobServicePackageName, BlobServiceVersion));
    for (const auto& p : options.PerOperationPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(
        std::make_unique<Azure::Core::Http::RetryPolicy>(Azure::Core::Http::RetryOptions()));
    for (const auto& p : options.PerRetryPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(std::make_unique<CommonHeadersRequestPolicy>());
    policies.emplace_back(std::make_unique<SharedKeyPolicy>(credential));
    policies.emplace_back(std::make_unique<Azure::Core::Http::TransportPolicy>(
        std::make_shared<Azure::Core::Http::CurlTransport>()));
    m_pipeline = std::make_shared<Azure::Core::Http::HttpPipeline>(policies);
  }

  BlobServiceClient::BlobServiceClient(
      const std::string& serviceUri,
      std::shared_ptr<Core::Credentials::TokenCredential> credential,
      const BlobServiceClientOptions& options)
      : m_serviceUrl(serviceUri)
  {
    std::vector<std::unique_ptr<Azure::Core::Http::HttpPolicy>> policies;
    policies.emplace_back(std::make_unique<Azure::Core::Http::TelemetryPolicy>(
        Details::c_BlobServicePackageName, BlobServiceVersion));
    for (const auto& p : options.PerOperationPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(
        std::make_unique<Azure::Core::Http::RetryPolicy>(Azure::Core::Http::RetryOptions()));
    for (const auto& p : options.PerRetryPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(std::make_unique<CommonHeadersRequestPolicy>());
    policies.emplace_back(
        std::make_unique<Core::Credentials::Policy::BearerTokenAuthenticationPolicy>(
            credential, Details::c_StorageScope));
    policies.emplace_back(std::make_unique<Azure::Core::Http::TransportPolicy>(
        std::make_shared<Azure::Core::Http::CurlTransport>()));
    m_pipeline = std::make_shared<Azure::Core::Http::HttpPipeline>(policies);
  }

  BlobServiceClient::BlobServiceClient(
      const std::string& serviceUri,
      const BlobServiceClientOptions& options)
      : m_serviceUrl(serviceUri)
  {
    std::vector<std::unique_ptr<Azure::Core::Http::HttpPolicy>> policies;
    policies.emplace_back(std::make_unique<Azure::Core::Http::TelemetryPolicy>(
        Details::c_BlobServicePackageName, BlobServiceVersion));
    for (const auto& p : options.PerOperationPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(
        std::make_unique<Azure::Core::Http::RetryPolicy>(Azure::Core::Http::RetryOptions()));
    for (const auto& p : options.PerRetryPolicies)
    {
      policies.emplace_back(p->Clone());
    }
    policies.emplace_back(std::make_unique<CommonHeadersRequestPolicy>());
    policies.emplace_back(std::make_unique<Azure::Core::Http::TransportPolicy>(
        std::make_shared<Azure::Core::Http::CurlTransport>()));
    m_pipeline = std::make_shared<Azure::Core::Http::HttpPipeline>(policies);
  }

  BlobContainerClient BlobServiceClient::GetBlobContainerClient(
      const std::string& containerName) const
  {
    auto containerUri = m_serviceUrl;
    containerUri.AppendPath(containerName);
    return BlobContainerClient(std::move(containerUri), m_pipeline);
  }

  Azure::Core::Response<ListContainersSegment> BlobServiceClient::ListBlobContainersSegment(
      const ListBlobContainersOptions& options) const
  {
    BlobRestClient::Service::ListBlobContainersOptions protocolLayerOptions;
    protocolLayerOptions.Prefix = options.Prefix;
    protocolLayerOptions.Marker = options.Marker;
    protocolLayerOptions.MaxResults = options.MaxResults;
    protocolLayerOptions.IncludeMetadata = options.Include;
    return BlobRestClient::Service::ListBlobContainers(
        options.Context, *m_pipeline, m_serviceUrl.ToString(), protocolLayerOptions);
  }

  Azure::Core::Response<UserDelegationKey> BlobServiceClient::GetUserDelegationKey(
      const std::string& startsOn,
      const std::string& expiresOn,
      const GetUserDelegationKeyOptions& options) const
  {
    BlobRestClient::Service::GetUserDelegationKeyOptions protocolLayerOptions;
    protocolLayerOptions.StartsOn = startsOn;
    protocolLayerOptions.ExpiresOn = expiresOn;
    return BlobRestClient::Service::GetUserDelegationKey(
        options.Context, *m_pipeline, m_serviceUrl.ToString(), protocolLayerOptions);
  }

  Azure::Core::Response<SetServicePropertiesInfo> BlobServiceClient::SetProperties(
      BlobServiceProperties properties,
      const SetBlobServicePropertiesOptions& options) const
  {
    BlobRestClient::Service::SetPropertiesOptions protocolLayerOptions;
    protocolLayerOptions.Properties = std::move(properties);
    return BlobRestClient::Service::SetProperties(
        options.Context, *m_pipeline, m_serviceUrl.ToString(), protocolLayerOptions);
  }

  Azure::Core::Response<BlobServiceProperties> BlobServiceClient::GetProperties(
      const GetBlobServicePropertiesOptions& options) const
  {
    BlobRestClient::Service::GetPropertiesOptions protocolLayerOptions;
    return BlobRestClient::Service::GetProperties(
        options.Context, *m_pipeline, m_serviceUrl.ToString(), protocolLayerOptions);
  }

}}} // namespace Azure::Storage::Blobs
