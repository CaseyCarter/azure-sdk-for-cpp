
#include "http/curl/curl.hpp"

#include "azure.hpp"
#include "http/http.hpp"

#include <string>

using namespace Azure::Core::Http;

std::unique_ptr<RawResponse> CurlTransport::Send(Context& context, Request& request)
{
  // Create CurlSession to perform request
  auto session = std::make_unique<CurlSession>(request);

  auto performing = session->Perform(context);

  if (performing != CURLE_OK)
  {
    switch (performing)
    {
      case CURLE_COULDNT_RESOLVE_HOST:
      {
        throw Azure::Core::Http::CouldNotResolveHostException(
            "Could not resolve host " + request.GetHost());
      }
      default:
      {
        throw Azure::Core::Http::TransportException(
            "Error while sending request. " + std::string(curl_easy_strerror(performing)));
      }
    }
  }

  // Move Response out of the session
  auto response = session->GetResponse();
  // Move the ownership of the CurlSession (bodyStream) to the response
  response->SetBodyStream(std::move(session));
  return response;
}

CURLcode CurlSession::Perform(Context& context)
{
  AZURE_UNREFERENCED_PARAMETER(context);

  // Working with Body Buffer. let Libcurl use the classic callback to read/write
  auto result = SetUrl();
  if (result != CURLE_OK)
  {
    return result;
  }

  // Make sure host is set
  // TODO-> use isEqualNoCase here once it is merged
  {
    auto headers = this->m_request.GetHeaders();
    auto hostHeader = headers.find("Host");
    if (hostHeader == headers.end())
    {
      this->m_request.AddHeader("Host", this->m_request.GetHost());
    }
    auto isContentLengthHeaderInRequest = headers.find("content-length");
    if (isContentLengthHeaderInRequest == headers.end())
    {
      this->m_request.AddHeader(
          "content-length", std::to_string(this->m_request.GetBodyStream()->Length()));
    }
  }

  result = SetConnectOnly();
  if (result != CURLE_OK)
  {
    return result;
  }

  // curl_easy_setopt(this->m_pCurl, CURLOPT_VERBOSE, 1L);
  // Set timeout to 24h. Libcurl will fail uploading on windows if timeout is:
  // timeout >= 25 days. Fails as soon as trying to upload any data
  // 25 days < timeout > 1 days. Fail on huge uploads ( > 1GB)
  curl_easy_setopt(this->m_pCurl, CURLOPT_TIMEOUT, 60L * 60L * 24L);

  // use expect:100 for PUT requests. Server will decide if it can take our request
  if (this->m_request.GetMethod() == HttpMethod::Put)
  {
    this->m_request.AddHeader("expect", "100-continue");
  }

  // establish connection only (won't send or receive anything yet)
  result = curl_easy_perform(this->m_pCurl);
  if (result != CURLE_OK)
  {
    return result;
  }
  // Record socket to be used
  result = curl_easy_getinfo(this->m_pCurl, CURLINFO_ACTIVESOCKET, &this->m_curlSocket);
  if (result != CURLE_OK)
  {
    return result;
  }

  // Send request
  result = HttpRawSend(context);
  if (result != CURLE_OK)
  {
    return result;
  }

  ReadStatusLineAndHeadersFromRawResponse();

  // Upload body for PUT
  if (this->m_request.GetMethod() != HttpMethod::Put)
  {
    return result;
  }

  // Check server response from Expect:100-continue for PUT;
  // This help to prevent us from start uploading data when Server can't handle it
  if (this->m_response->GetStatusCode() != HttpStatusCode::Continue)
  {
    return result; // Won't upload.
  }

  // Start upload
  result = this->UploadBody(context);
  if (result != CURLE_OK)
  {
    return result; // will throw transport exception before trying to read
  }
  ReadStatusLineAndHeadersFromRawResponse();
  return result;
}

// Creates an HTTP Response with specific bodyType
static std::unique_ptr<RawResponse> CreateHTTPResponse(
    uint8_t const* const begin,
    uint8_t const* const last)
{
  // set response code, http version and reason phrase (i.e. HTTP/1.1 200 OK)
  auto start = begin + 5; // HTTP = 4, / = 1, moving to 5th place for version
  auto end = std::find(start, last, '.');
  auto majorVersion = std::stoi(std::string(start, end));

  start = end + 1; // start of minor version
  end = std::find(start, last, ' ');
  auto minorVersion = std::stoi(std::string(start, end));

  start = end + 1; // start of status code
  end = std::find(start, last, ' ');
  auto statusCode = std::stoi(std::string(start, end));

  start = end + 1; // start of reason phrase
  end = std::find(start, last, '\r');
  auto reasonPhrase = std::string(start, end); // remove \r

  // allocate the instance of response to heap with shared ptr
  // So this memory gets delegated outside Curl Transport as a shared ptr so memory will be
  // eventually released
  return std::make_unique<RawResponse>(
      (uint16_t)majorVersion, (uint16_t)minorVersion, HttpStatusCode(statusCode), reasonPhrase);
}

// Creates an HTTP Response with specific bodyType
static std::unique_ptr<RawResponse> CreateHTTPResponse(std::string const& header)
{
  return CreateHTTPResponse(
      reinterpret_cast<const uint8_t*>(header.data()),
      reinterpret_cast<const uint8_t*>(header.data() + header.size()));
}

// To wait for a socket to be ready to be read/write
static int WaitForSocketReady(curl_socket_t sockfd, int for_recv, long timeout_ms)
{
  struct timeval tv;
  fd_set infd, outfd, errfd;
  int res;

  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  FD_ZERO(&infd);
  FD_ZERO(&outfd);
  FD_ZERO(&errfd);

  FD_SET(sockfd, &errfd); /* always check for error */

  if (for_recv)
  {
    FD_SET(sockfd, &infd);
  }
  else
  {
    FD_SET(sockfd, &outfd);
  }

  /* select() returns the number of signalled sockets or -1 */
  res = select((int)sockfd + 1, &infd, &outfd, &errfd, &tv);
  return res;
}

bool CurlSession::isUploadRequest()
{
  return this->m_request.GetMethod() == HttpMethod::Put
      || this->m_request.GetMethod() == HttpMethod::Post;
}

CURLcode CurlSession::SetUrl()
{
  return curl_easy_setopt(this->m_pCurl, CURLOPT_URL, this->m_request.GetEncodedUrl().data());
}

CURLcode CurlSession::SetConnectOnly()
{
  return curl_easy_setopt(this->m_pCurl, CURLOPT_CONNECT_ONLY, 1L);
}

// Send buffer thru the wire
CURLcode CurlSession::SendBuffer(uint8_t const* buffer, size_t bufferSize)
{
  for (size_t sentBytesTotal = 0; sentBytesTotal < bufferSize;)
  {
    for (CURLcode sendResult = CURLE_AGAIN; sendResult == CURLE_AGAIN;)
    {
      size_t sentBytesPerRequest = 0;
      sendResult = curl_easy_send(
          this->m_pCurl,
          buffer + sentBytesTotal,
          bufferSize - sentBytesTotal,
          &sentBytesPerRequest);

      switch (sendResult)
      {
        case CURLE_OK:
          sentBytesTotal += sentBytesPerRequest;
          this->m_uploadedBytes += sentBytesPerRequest;
          break;
        case CURLE_AGAIN:
          if (!WaitForSocketReady(this->m_curlSocket, 0, 60000L))
          {
            // TODO: Change this to something more relevant
            throw;
          }
          break;
        default:
          return sendResult;
      }
    };
  }

  return CURLE_OK;
}

CURLcode CurlSession::UploadBody(Context& context)
{
  // Send body UploadStreamPageSize at a time (libcurl default)
  // NOTE: if stream is on top a contiguous memory, we can avoid allocating this copying buffer
  auto streamBody = this->m_request.GetBodyStream();
  CURLcode sendResult = CURLE_OK;
  this->m_uploadedBytes = 0;

  int64_t uploadChunkSize = this->m_request.GetUploadChunkSize();
  if (uploadChunkSize <= 0)
  {
    // use default size
    uploadChunkSize = Details::c_UploadDefaultChunkSize;
  }
  auto unique_buffer = std::make_unique<uint8_t[]>(static_cast<size_t>(uploadChunkSize));

  while (true)
  {
    auto rawRequestLen = streamBody->Read(context, unique_buffer.get(), uploadChunkSize);
    if (rawRequestLen == 0)
    {
      break;
    }
    sendResult = SendBuffer(unique_buffer.get(), static_cast<size_t>(rawRequestLen));
    if (sendResult != CURLE_OK)
    {
      return sendResult;
    }
  }
  return sendResult;
}

// custom sending to wire an http request
CURLcode CurlSession::HttpRawSend(Context& context)
{
  // something like GET /path HTTP1.0 \r\nheaders\r\n
  auto rawRequest = this->m_request.GetHTTPMessagePreBody();
  int64_t rawRequestLen = rawRequest.size();

  CURLcode sendResult = SendBuffer(
      reinterpret_cast<uint8_t const*>(rawRequest.data()), static_cast<size_t>(rawRequestLen));

  if (sendResult != CURLE_OK || this->m_request.GetMethod() == HttpMethod::Put)
  {
    return sendResult;
  }

  auto streamBody = this->m_request.GetBodyStream();
  if (streamBody->Length() == 0)
  {
    // Finish request with no body (Head or PUT (expect:100;)
    uint8_t const endOfRequest = 0;
    return SendBuffer(&endOfRequest, 1); // need one more byte to end request
  }
  return this->UploadBody(context);
}

void CurlSession::ParseChunkSize()
{
  // Use this string to construct the chunk size. This is because we could have an internal
  // buffer like [headers...\r\n123], where 123 is chunk size but we still need to pull more
  // data fro wire to get the full chunkSize. Next data could be just [\r\n] or [456\r\n]
  auto strChunkSize = std::string();

  // Move to after chunk size
  for (bool keepPolling = true; keepPolling;)
  {
    for (int64_t index = this->m_bodyStartInBuffer, i = 0; index < this->m_innerBufferSize;
         index++, i++)
    {
      strChunkSize.append(reinterpret_cast<char*>(&this->m_readBuffer[index]), 1);
      if (i > 1 && this->m_readBuffer[index] == '\n')
      {
        // get chunk size. Chunk size comes in Hex value
        this->m_chunkSize = static_cast<int64_t>(std::stoull(strChunkSize, nullptr, 16));

        if (index + 1 == this->m_innerBufferSize)
        { // on last index. Whatever we read is the BodyStart here
          this->m_innerBufferSize
              = ReadSocketToBuffer(this->m_readBuffer, Details::c_LibcurlReaderSize);
          this->m_bodyStartInBuffer = 0;
        }
        else
        { // not at the end, buffer like [999 \r\nBody...]
          this->m_bodyStartInBuffer = index + 1;
        }
        keepPolling = false;
        break;
      }
    }
    if (keepPolling)
    { // Read all internal buffer and \n was not found, pull from wire
      this->m_innerBufferSize
          = ReadSocketToBuffer(this->m_readBuffer, Details::c_LibcurlReaderSize);
      this->m_bodyStartInBuffer = 0;
    }
  }
  return;
}

// Read status line plus headers to create a response with no body
void CurlSession::ReadStatusLineAndHeadersFromRawResponse()
{
  auto parser = ResponseBufferParser();
  auto bufferSize = int64_t();

  // Keep reading until all headers were read
  while (!parser.IsParseCompleted())
  {
    // Try to fill internal buffer from socket.
    // If response is smaller than buffer, we will get back the size of the response
    bufferSize = ReadSocketToBuffer(this->m_readBuffer, Details::c_LibcurlReaderSize);

    // returns the number of bytes parsed up to the body Start
    auto bytesParsed = parser.Parse(this->m_readBuffer, static_cast<size_t>(bufferSize));

    if (bytesParsed < bufferSize)
    {
      this->m_bodyStartInBuffer = bytesParsed; // Body Start
    }
  }

  this->m_response = parser.GetResponse();
  this->m_innerBufferSize = static_cast<size_t>(bufferSize);

  // For Head request, set the length of body response to 0.
  // Response will give us content-length as if we were not doing Head saying what would it be the
  // length of the body. However, Server won't send body
  if (this->m_request.GetMethod() == HttpMethod::Head)
  {
    this->m_contentLength = 0;
    this->m_bodyStartInBuffer = -1;
    this->m_rawResponseEOF = true;
    return;
  }

  // headers are already loweCase at this point
  auto headers = this->m_response->GetHeaders();

  auto isContentLengthHeaderInResponse = headers.find("content-length");
  if (isContentLengthHeaderInResponse != headers.end())
  {
    this->m_contentLength
        = static_cast<int64_t>(std::stoull(isContentLengthHeaderInResponse->second.data()));
    return;
  }

  this->m_contentLength = -1;
  auto isTransferEncodingHeaderInResponse = headers.find("transfer-encoding");
  if (isTransferEncodingHeaderInResponse != headers.end())
  {
    auto headerValue = isTransferEncodingHeaderInResponse->second;
    auto isChunked = headerValue.find("chunked");

    if (isChunked != std::string::npos)
    {
      // set curl session to know response is chunked
      // This will be used to remove chunked info while reading
      this->m_isChunkedResponseType = true;

      // Need to move body start after chunk size
      if (this->m_bodyStartInBuffer == -1)
      { // if nothing on inner buffer, pull from wire
        this->m_innerBufferSize
            = ReadSocketToBuffer(this->m_readBuffer, Details::c_LibcurlReaderSize);
        this->m_bodyStartInBuffer = 0;
      }

      ParseChunkSize();
      return;
    }
  }
  /*
  https://tools.ietf.org/html/rfc7230#section-3.3.3
   7.  Otherwise, this is a response message without a declared message
       body length, so the message body length is determined by the
       number of octets received prior to the server closing the
       connection.
  */
}

// Read from curl session
int64_t CurlSession::Read(Azure::Core::Context& context, uint8_t* buffer, int64_t count)
{
  context.ThrowIfCanceled();

  if (count <= 0)
  {
    // LimitStream would try to read 0 bytes
    return 0;
  }

  // check if all chunked is read already
  if (this->m_isChunkedResponseType && this->m_chunkSize == 0)
  {
    // Need to read CRLF after all chunk was read
    for (int8_t i = 0; i < 2; i++)
    {
      if (this->m_bodyStartInBuffer > 0 && this->m_bodyStartInBuffer < this->m_innerBufferSize)
      {
        this->m_bodyStartInBuffer += 1;
      }
      else
      { // end of buffer, pull data from wire
        this->m_innerBufferSize
            = ReadSocketToBuffer(this->m_readBuffer, Details::c_LibcurlReaderSize);
        this->m_bodyStartInBuffer = 1; // jump first char (could be \r or \n)
      }
    }
    // get the size of next chunk
    ParseChunkSize();

    if (this->m_chunkSize == 0)
    {
      // end of transfer
      this->m_rawResponseEOF = true;
      return 0;
    }
  }

  auto totalRead = int64_t();
  auto readRequestLength
      = this->m_isChunkedResponseType ? std::min(this->m_chunkSize, count) : count;

  // For responses with content-length, avoid trying to read beyond Content-length or
  // libcurl could return a second response as BadRequest.
  // https://github.com/Azure/azure-sdk-for-cpp/issues/306
  if (this->m_contentLength > 0)
  {
    auto remainingBodyContent = this->m_contentLength - this->m_sessionTotalRead;
    readRequestLength = std::min(readRequestLength, remainingBodyContent);
  }

  // Take data from inner buffer if any
  if (this->m_bodyStartInBuffer >= 0)
  {
    // still have data to take from innerbuffer
    MemoryBodyStream innerBufferMemoryStream(
        this->m_readBuffer + this->m_bodyStartInBuffer,
        this->m_innerBufferSize - this->m_bodyStartInBuffer);

    totalRead = innerBufferMemoryStream.Read(context, buffer, readRequestLength);
    this->m_bodyStartInBuffer += totalRead;
    this->m_sessionTotalRead += totalRead;
    if (this->m_isChunkedResponseType)
    {
      this->m_chunkSize -= totalRead;
    }

    if (this->m_bodyStartInBuffer == this->m_innerBufferSize)
    {
      this->m_bodyStartInBuffer = -1; // read everyting from inner buffer already
    }
    return totalRead;
  }

  // Head request have contentLength = 0, so we won't read more, just return 0
  // Also if we have already read all contentLength
  if (this->m_sessionTotalRead == this->m_contentLength || this->m_rawResponseEOF)
  {
    // Read everything already
    return 0;
  }

  // Read from socket when no more data on internal buffer
  // For chunk request, read a chunk based on chunk size
  totalRead = ReadSocketToBuffer(buffer, static_cast<size_t>(readRequestLength));
  this->m_sessionTotalRead += totalRead;
  if (this->m_isChunkedResponseType)
  {
    this->m_chunkSize -= totalRead;
  }

  return totalRead;
}

// Read from socket and return the number of bytes taken from socket
int64_t CurlSession::ReadSocketToBuffer(uint8_t* buffer, int64_t bufferSize)
{
  // loop until read result is not CURLE_AGAIN
  size_t readBytes = 0;
  for (CURLcode readResult = CURLE_AGAIN; readResult == CURLE_AGAIN;)
  {
    readResult = curl_easy_recv(this->m_pCurl, buffer, static_cast<size_t>(bufferSize), &readBytes);

    switch (readResult)
    {
      case CURLE_AGAIN:
        if (!WaitForSocketReady(this->m_curlSocket, 0, 60000L))
        {
          // TODO: Change this to somehing more relevant
          throw Azure::Core::Http::TransportException(
              "Timeout waiting to read from Network socket");
        }
        break;
      case CURLE_OK:
        break;
      default:
        // Error reading from socket
        throw Azure::Core::Http::TransportException("Error while reading from network socket");
    }
  }
  return readBytes;
}

std::unique_ptr<Azure::Core::Http::RawResponse> CurlSession::GetResponse()
{
  return std::move(this->m_response);
}

int64_t CurlSession::ResponseBufferParser::Parse(
    uint8_t const* const buffer,
    int64_t const bufferSize)
{
  if (this->m_parseCompleted)
  {
    return 0;
  }

  // Read all buffer until \r\n is found
  int64_t start = 0, index = 0;
  for (; index < bufferSize; index++)
  {
    if (buffer[index] == '\r')
    {
      this->m_delimiterStartInPrevPosition = true;
      continue;
    }

    if (buffer[index] == '\n' && this->m_delimiterStartInPrevPosition)
    {
      // found end of delimiter
      if (this->m_internalBuffer.size() > 0) // Check internal buffer
      {
        // At this point, we are reading to append more to internal buffer.
        // Only append more if index is greater than 1, meaning not when buffer is [\r\nxxx]
        // only on buffer like [xxx\r\n yyyy], append xxx
        if (index > 1)
        {
          // Previously appended something
          this->m_internalBuffer.append(buffer + start, buffer + index - 1); // minus 1 to remove \r
        }
        if (this->state == ResponseParserState::StatusLine)
        {
          // Create Response
          this->m_response = CreateHTTPResponse(this->m_internalBuffer);
          // Set state to headers
          this->state = ResponseParserState::Headers;
          this->m_delimiterStartInPrevPosition = false;
          start = index + 1; // jump \n
        }
        else if (this->state == ResponseParserState::Headers)
        {
          // Add header. TODO: Do toLower so all headers are lowerCase
          this->m_response->AddHeader(this->m_internalBuffer);
          this->m_delimiterStartInPrevPosition = false;
          start = index + 1; // jump \n
        }
        else
        {
          // Should never happen that parser is not statusLIne or Headers and we still try to parse
          // more.
          throw;
        }
        // clean internal buffer
        this->m_internalBuffer.clear();
      }
      else
      {
        // Nothing at internal buffer. Add directly from internal buffer
        if (this->state == ResponseParserState::StatusLine)
        {
          // Create Response
          this->m_response = CreateHTTPResponse(buffer + start, buffer + index - 1);
          // Set state to headers
          this->state = ResponseParserState::Headers;
          this->m_delimiterStartInPrevPosition = false;
          start = index + 1; // jump \n
        }
        else if (this->state == ResponseParserState::Headers)
        {
          // Check if this is end of headers delimiter
          // 1) internal buffer is empty and \n is the first char on buffer [\nBody...]
          // 2) index == start + 1. No header data after last \r\n [header\r\n\r\n]
          if (index == 0 || index == start + 1)
          {
            this->m_parseCompleted = true;
            return index + 1; // plus 1 to advance the \n. If we were at buffer end.
          }

          // Add header. TODO: Do toLower so all headers are lowerCase
          this->m_response->AddHeader(buffer + start, buffer + index - 1);
          this->m_delimiterStartInPrevPosition = false;
          start = index + 1; // jump \n
        }
        else
        {
          // Should never happen that parser is not statusLIne or Headers and we still try to parse
          // more.
          throw;
        }
      }
    }
    else
    {
      if (index == 0 && this->m_internalBuffer.size() > 0 && this->m_delimiterStartInPrevPosition)
      {
        // unlikely. But this means a case with buffers like [xx\r], [xxxx]
        // \r is not delimiter and in previous loop it was omitted, so adding it now
        this->m_internalBuffer.append("\r");
      }
      // \r in the response without \n after it. keep parsing
      this->m_delimiterStartInPrevPosition = false;
    }
  }

  if (start < bufferSize)
  {
    // didn't find the end of delimiter yet, save at internal buffer
    // If this->m_delimiterStartInPrevPosition is true, buffer ends in \r [xxxx\r]
    // Don't add \r. IF next char is not \n, we will append \r then on next loop
    this->m_internalBuffer.append(
        buffer + start, buffer + bufferSize - (this->m_delimiterStartInPrevPosition ? 1 : 0));
  }

  return index;
}

// Finds delimiter '\r' as the end of the
int64_t CurlSession::ResponseBufferParser::BuildStatusCode(
    uint8_t const* const buffer,
    int64_t const bufferSize)
{
  if (this->state != ResponseParserState::StatusLine)
  {
    return 0; // Wrong internal state to call this method.
  }

  uint8_t endOfStatusLine = '\r';
  auto endOfBuffer = buffer + bufferSize;

  // Look for the end of status line in buffer
  auto indexOfEndOfStatusLine = std::find(buffer, endOfBuffer, endOfStatusLine);

  if (indexOfEndOfStatusLine == endOfBuffer)
  {
    // did not find the delimiter yet, copy to internal buffer
    this->m_internalBuffer.append(buffer, endOfBuffer);
    return bufferSize; // all buffer read and requesting for more
  }

  // Delimiter found, check if there is data in the internal buffer
  if (this->m_internalBuffer.size() > 0)
  {
    // If the index is same as buffer it means delimiter is at position 0, meaning that
    // internalBuffer containst the status line and we don't need to add anything else
    if (indexOfEndOfStatusLine > buffer)
    {
      // Append and build response minus the delimiter
      this->m_internalBuffer.append(buffer, indexOfEndOfStatusLine);
    }
    this->m_response = CreateHTTPResponse(this->m_internalBuffer);
  }
  else
  {
    // Internal Buffer was not required, create response directly from buffer
    this->m_response = CreateHTTPResponse(std::string(buffer, indexOfEndOfStatusLine));
  }

  // update control
  this->state = ResponseParserState::Headers;
  this->m_internalBuffer.clear();

  // Return the index of the next char to read after delimiter
  // No need to advance one more char ('\n') (since we might be at the end of the array)
  // Parsing Headers will make sure to move one possition
  return indexOfEndOfStatusLine + 1 - buffer;
}

// Finds delimiter '\r' as the end of the
int64_t CurlSession::ResponseBufferParser::BuildHeader(
    uint8_t const* const buffer,
    int64_t const bufferSize)
{
  if (this->state != ResponseParserState::Headers)
  {
    return 0; // can't run this if state is not Headers.
  }

  uint8_t delimiter = '\r';
  auto start = buffer;
  auto endOfBuffer = buffer + bufferSize;

  if (bufferSize == 1 && buffer[0] == '\n')
  {
    // rare case of using buffer of size 1 to read. In this case, if the char is next value after
    // headers or previous header, just consider it as read and return
    return bufferSize;
  }
  else if (bufferSize > 1 && this->m_internalBuffer.size() == 0) // only if nothing in buffer,
                                                                 // advance
  {
    // move offset one possition. This is because readStatusLine and readHeader will read up to
    // '\r' then next delimeter is '\n' and we don't care
    start = buffer + 1;
  }

  // Look for the end of status line in buffer
  auto indexOfEndOfStatusLine = std::find(start, endOfBuffer, delimiter);

  if (indexOfEndOfStatusLine == start && this->m_internalBuffer.size() == 0)
  {
    // \r found at the start means the end of headers
    this->m_internalBuffer.clear();
    this->m_parseCompleted = true;
    return 1; // can't return more than the found delimiter. On read remaining we need to also
              // remove first char
  }

  if (indexOfEndOfStatusLine == endOfBuffer)
  {
    // did not find the delimiter yet, copy to internal buffer
    this->m_internalBuffer.append(start, endOfBuffer);
    return bufferSize; // all buffer read and requesting for more
  }

  // Delimiter found, check if there is data in the internal buffer
  if (this->m_internalBuffer.size() > 0)
  {
    // If the index is same as buffer it means delimiter is at position 0, meaning that
    // internalBuffer containst the status line and we don't need to add anything else
    if (indexOfEndOfStatusLine > buffer)
    {
      // Append and build response minus the delimiter
      this->m_internalBuffer.append(start, indexOfEndOfStatusLine);
    }
    this->m_response->AddHeader(this->m_internalBuffer);
  }
  else
  {
    // Internal Buffer was not required, create response directly from buffer
    this->m_response->AddHeader(std::string(start, indexOfEndOfStatusLine));
  }

  // reuse buffer
  this->m_internalBuffer.clear();

  // Return the index of the next char to read after delimiter
  // No need to advance one more char ('\n') (since we might be at the end of the array)
  // Parsing Headers will make sure to move one possition
  return indexOfEndOfStatusLine + 1 - buffer;
}
