// Compatibility header for Boost 1.85+
// io_service was replaced by io_context
#ifndef BOOST_ASIO_IO_SERVICE_HPP
#define BOOST_ASIO_IO_SERVICE_HPP

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

namespace boost {
namespace asio {
    using io_service = io_context;
}
}

#endif // BOOST_ASIO_IO_SERVICE_HPP
