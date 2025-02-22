/* libssh2 test receiving large amounts of data through a channel */

#include "runner.h"

#include <stdlib.h>  /* for getenv() */

/* set in Dockerfile */
static const char *USERNAME = "libssh2";
static const char *KEY_FILE_PRIVATE = "key_rsa";
static const char *KEY_FILE_PUBLIC = "key_rsa.pub";

int test(LIBSSH2_SESSION *session)
{
    int rc;
    unsigned long xfer_bytes = 0;
    LIBSSH2_CHANNEL *channel;

    /* Size and number of blocks to transfer
     * This needs to be large to increase the chance of timing effects causing
     * different code paths to be hit in the unframing code, but not so long
     * that the integration tests take too long. 5 seconds of run time is
     * probably a reasonable compromise. The block size is an odd number to
     * increase the chance that various internal buffer and block boundaries
     * are overlapped. */
    const unsigned long xfer_bs = 997;
    unsigned long xfer_count = 140080;

    char remote_command[256];
    const char *env;

    const char *userauth_list =
        libssh2_userauth_list(session, USERNAME,
                              (unsigned int)strlen(USERNAME));
    if(!userauth_list) {
        print_last_session_error("libssh2_userauth_list");
        return 1;
    }

    if(!strstr(userauth_list, "publickey")) {
        fprintf(stderr, "'publickey' was expected in userauth list: %s\n",
                userauth_list);
        return 1;
    }

    rc = libssh2_userauth_publickey_fromfile_ex(session, USERNAME,
                                                (unsigned int)strlen(USERNAME),
                                                srcdir_path(KEY_FILE_PUBLIC),
                                                srcdir_path(KEY_FILE_PRIVATE),
                                                NULL);
    if(rc) {
        print_last_session_error("libssh2_userauth_publickey_fromfile_ex");
        return 1;
    }

    /* Request a session channel on which to run a shell */
    channel = libssh2_channel_open_session(session);
    if(!channel) {
        fprintf(stderr, "Unable to open a session\n");
        goto shutdown;
    }

    env = getenv("FIXTURE_XFER_COUNT");
    if(env) {
        xfer_count = (unsigned long)strtol(env, NULL, 0);
        fprintf(stderr, "Custom xfer_count: %lu\n", xfer_count);
    }

    /* command to transfer the desired amount of data */
    snprintf(remote_command, sizeof(remote_command),
             "dd if=/dev/zero bs=%lu count=%lu status=none",
             xfer_bs, xfer_count);

    /* Send the command to transfer data */
    if(libssh2_channel_exec(channel, remote_command)) {
        fprintf(stderr, "Unable to request command on channel\n");
        goto shutdown;
    }

    /* Read data */
    while(!libssh2_channel_eof(channel)) {
        char buf[1024];
        ssize_t err = libssh2_channel_read(channel, buf, sizeof(buf));
        if(err < 0)
            fprintf(stderr, "Unable to read response: %d\n", (int)err);
        else {
            unsigned int i;
            for(i = 0; i < (unsigned long)err; ++i) {
                if(buf[i]) {
                    fprintf(stderr, "Bad data received\n");
                    /* Test will fail below due to bad data length */
                    break;
                }
            }
            xfer_bytes += i;
        }
    }

    /* Shut down */
    if(libssh2_channel_close(channel))
        fprintf(stderr, "Unable to close channel\n");

    if(channel) {
        libssh2_channel_free(channel);
        channel = NULL;
    }

shutdown:

    /* Test check */
    if(xfer_bytes != xfer_count * xfer_bs) {
        fprintf(stderr, "Not enough bytes received: %lu not %lu\n",
                xfer_bytes, xfer_count * xfer_bs);
        return 1;  /* error */
    }
    return 0;
}
