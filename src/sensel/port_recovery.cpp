#include "port_recovery.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace sensel::detail {
namespace {

constexpr auto recoveryDeadline = std::chrono::milliseconds(250);

class ScopedFd {
public:
    explicit ScopedFd(int fd) noexcept : fd_(fd) {}
    ~ScopedFd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    int get() const noexcept { return fd_; }

private:
    int fd_ = -1;
};

bool waitForFd(int fd, short events, std::chrono::steady_clock::time_point deadline) noexcept {
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = events;
        const int timeout = static_cast<int>(std::max<std::int64_t>(1, remaining.count()));
        const int result = ::poll(&descriptor, 1, timeout);
        if (result > 0) {
            return (descriptor.revents & events) != 0;
        }
        if (result == 0) {
            return false;
        }
        if (errno != EINTR) {
            return false;
        }
    }
}

bool writeAll(int fd,
              const unsigned char* data,
              std::size_t size,
              std::chrono::steady_clock::time_point deadline) noexcept {
    std::size_t written = 0;
    while (written < size) {
        const auto result = ::write(fd, data + written, size - written);
        if (result > 0) {
            written += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
            waitForFd(fd, POLLOUT, deadline)) {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

bool recoverExplicitPort(const std::string& path) noexcept {
    const int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (fd < 0) {
        return false;
    }
    ScopedFd port(fd);

    termios options{};
    if (::tcgetattr(port.get(), &options) != 0) {
        return false;
    }

    options.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    options.c_oflag &= ~OPOST;
    options.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    options.c_cflag &= ~(CSIZE | PARENB);
    options.c_cflag |= CS8;
    if (::cfsetispeed(&options, B115200) != 0 ||
        ::cfsetospeed(&options, B115200) != 0 ||
        ::tcsetattr(port.get(), TCSANOW, &options) != 0) {
        return false;
    }

    // SCAN_ENABLED (0x25) = disabled. This is intentionally sent only to a
    // caller-supplied path after the caller explicitly opts into recovery.
    constexpr std::array<unsigned char, 5> stopScanning{0x01, 0x25, 0x01, 0x00, 0x00};
    const auto deadline = std::chrono::steady_clock::now() + recoveryDeadline;
    if (!writeAll(port.get(), stopScanning.data(), stopScanning.size(), deadline)) {
        return false;
    }

    std::array<unsigned char, 4096> discarded{};
    while (std::chrono::steady_clock::now() < deadline) {
        for (;;) {
            const auto count = ::read(port.get(), discarded.data(), discarded.size());
            if (count > 0) {
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                return false;
            }
            break;
        }
        (void)waitForFd(port.get(), POLLIN, deadline);
    }

    return ::tcflush(port.get(), TCIOFLUSH) == 0;
}

} // namespace sensel::detail
