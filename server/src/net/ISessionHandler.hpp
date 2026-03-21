#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <botan/asio_stream.h>

using AwaitableTcpStream = typename boost::beast::tcp_stream::rebind_executor<
    boost::asio::use_awaitable_t<>::executor_with_default<
        boost::asio::any_io_executor>>::other;

using TlsStream = Botan::TLS::Stream<AwaitableTcpStream&>;

/**
 * Abstract session handler that the TLS engine delegates to after
 * completing the handshake.  Both the HTTP layer and the CLI layer
 * implement this interface so that QuantumSafeTlsEngine remains
 * protocol-agnostic.
 */
class ISessionHandler {
public:
    virtual ~ISessionHandler() = default;

    /**
     * Run the protocol-specific session loop over an already-established
     * TLS stream.  Implementations own the read/write loop and throw
     * (or co_return) when the session should end.
     */
    virtual boost::asio::awaitable<void> handle_session(TlsStream& stream) = 0;
};
