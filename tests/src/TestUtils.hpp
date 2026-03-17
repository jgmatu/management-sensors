#pragma once
#include <ostream>
#include <sstream>
class ScopedStreamSilencer {
public:
    explicit ScopedStreamSilencer(std::ostream& os)
        : os_(os),
          old_buf_(os.rdbuf()),
          null_stream_()
    {
        os_.rdbuf(null_stream_.rdbuf());  // redirige a un ostringstream interno
    }
    ~ScopedStreamSilencer() {
        os_.rdbuf(old_buf_);              // restaura el buffer original
    }
private:
    std::ostream&   os_;
    std::streambuf* old_buf_;
    std::ostringstream null_stream_;
};