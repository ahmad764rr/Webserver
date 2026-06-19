#include "core/ClientConnection.hpp"
#include <unistd.h>

CgiTask::CgiTask()
    : active(false),
      pid(-1),
      pipe_in_fd(-1),
      pipe_out_fd(-1),
      body_written(0),
      start_time_ms(0),
      process_exited(false),
      exit_status(0) {
}

void CgiTask::cleanup() {
    if (pipe_in_fd != -1) {
        close(pipe_in_fd);
        pipe_in_fd = -1;
    }
    if (pipe_out_fd != -1) {
        close(pipe_out_fd);
        pipe_out_fd = -1;
    }
    active = false;
    process_exited = false;
    exit_status = 0;
}

ClientConnection::ClientConnection()
    : fd(-1),
      serverIndex(0),
      hasResponse(false),
      closeAfterSend(false),
      sendFileFd(-1) {
}
