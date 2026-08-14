
// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

// A drop-in replacement for the small subset of the libgps API that gpsd_averaged uses, reading NMEA 0183
// directly from a serial GPS instead of talking to gpsd. This removes gpsd and libgps from the runtime on
// lightweight systems, at the cost of the extras gpsd provides (multi-client sharing, device autodetection,
// binary protocols, PPS). The device is opened read-only, so nothing is ever written to the receiver and
// whatever NMEA it emits by default is what gets parsed.
//
// Only GGA and GSA are used: GGA carries position, altitude, satellites-used and HDOP, and is emitted once
// per epoch; GSA carries the 2D/3D fix mode. A fix is reported to the caller (MODE_SET) only on GGA, so the
// caller sees exactly one fix per epoch, matching the cadence of gpsd's TPV reports. Reporting per sentence
// would count each epoch five or so times over and falsely shrink the averaged uncertainty.

#ifndef GPS_NMEA_H
#define GPS_NMEA_H

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

// ------------------------------------------------------------------------------------------------------------------------

// Present as API v9 so the caller uses the altMSL/altHAE split rather than the deprecated fix.altitude.
#define GPSD_API_MAJOR_VERSION 9

#define MODE_NOT_SEEN 0
#define MODE_NO_FIX 1
#define MODE_2D 2
#define MODE_3D 3

#define MODE_SET (1u << 0)
#define LATLON_SET (1u << 1)
#define ALTITUDE_SET (1u << 2)

#define WATCH_ENABLE (1u << 0)
#define WATCH_DISABLE (1u << 1)
#define WATCH_JSON (1u << 4)

#define GPS_NMEA_BUFFER 1024
#define GPS_NMEA_FIELDS 24
#define GPS_NMEA_BAUD_DEF B9600
#define GPS_NMEA_TALKER_SZ 3 // '$' plus the two character talker id, e.g. "$GP", "$GN"

struct gps_fix_t {
    int mode;
    double latitude, longitude;
    double altMSL, altHAE;
};

struct gps_dop_t {
    double hdop;
};

struct gps_data_t {
    int gps_fd;
    unsigned int set;
    struct gps_fix_t fix;
    struct gps_dop_t dop;
    int satellites_used;
    // shim private state
    char buffer[GPS_NMEA_BUFFER];
    size_t buffer_length;
    int gsa_mode;    // fix mode from the most recent GSA
    double gsa_hdop; // HDOP from the most recent GSA, used when GGA leaves the field empty
};

// ------------------------------------------------------------------------------------------------------------------------

static speed_t __gps_nmea_baud(const char *const baud) {
    switch (atoi(baud)) {
    case 4800:
        return B4800;
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    default:
        return GPS_NMEA_BAUD_DEF;
    }
}

static double __gps_nmea_number(const char *const field) {
    if (field == NULL || *field == '\0')
        return NAN;
    char *end;
    const double value = strtod(field, &end);
    return (end == field) ? NAN : value;
}

// NMEA angles are ddmm.mmmm / dddmm.mmmm with the hemisphere in a separate field.
static double __gps_nmea_degrees(const char *const field, const char *const hemisphere) {
    const double value = __gps_nmea_number(field);
    if (!isfinite(value) || hemisphere == NULL || *hemisphere == '\0')
        return NAN;
    const double degrees = floor(value / 100.0);
    const double result  = degrees + (value - degrees * 100.0) / 60.0;
    return (*hemisphere == 'S' || *hemisphere == 'W') ? -result : result;
}

static bool __gps_nmea_checksum(const char *const sentence) {
    const char *const star = strrchr(sentence, '*');
    if (star == NULL || strlen(star) < 3)
        return false;
    unsigned char sum = 0;
    for (const char *p = sentence + 1; p < star; p++)
        sum ^= (unsigned char)*p;
    return (unsigned char)strtoul(star + 1, NULL, 16) == sum;
}

// Splits in place; must be called only after the checksum has been verified, as it drops the "*CS" suffix.
// Counts are unsigned so that the bound checks carry no signed-overflow assumptions for the optimiser.
static size_t __gps_nmea_split(char *const sentence, const char **const fields, const size_t fields_max) {
    char *const star = strrchr(sentence, '*');
    if (star != NULL)
        *star = '\0';
    size_t count    = 0;
    fields[count++] = sentence;
    for (char *p = sentence; *p != '\0' && count < fields_max; p++)
        if (*p == ',') {
            *p              = '\0';
            fields[count++] = p + 1;
        }
    return count;
}

static const char *__gps_nmea_field(const char *const *const fields, const size_t count, const size_t index) { return (index < count) ? fields[index] : ""; }

static void __gps_nmea_sentence(struct gps_data_t *const gps_handle, char *const sentence) {
    if (!__gps_nmea_checksum(sentence))
        return;
    const char *fields[GPS_NMEA_FIELDS];
    const size_t count = __gps_nmea_split(sentence, fields, GPS_NMEA_FIELDS);
    if (count < 1 || strlen(fields[0]) < (size_t)(GPS_NMEA_TALKER_SZ + 3))
        return;
    const char *const type = fields[0] + GPS_NMEA_TALKER_SZ; // accept any talker: $GP, $GN, $GL, $GA, ...

    // GSA: [2] = fix mode (1 none, 2 = 2D, 3 = 3D), [16] = HDOP
    if (strcmp(type, "GSA") == 0) {
        gps_handle->gsa_mode = atoi(__gps_nmea_field(fields, count, 2));
        gps_handle->gsa_hdop = __gps_nmea_number(__gps_nmea_field(fields, count, 16));
        return;
    }
    // GGA: [2][3] = lat, [4][5] = lon, [6] = quality, [7] = satellites, [8] = HDOP, [9] = altitude MSL,
    //      [11] = geoid separation
    if (strcmp(type, "GGA") != 0)
        return;

    gps_handle->set = MODE_SET;
    if (atoi(__gps_nmea_field(fields, count, 6)) <= 0) { // 0 = fix unavailable
        gps_handle->fix.mode = MODE_NO_FIX;
        return;
    }

    const double altitude   = __gps_nmea_number(__gps_nmea_field(fields, count, 9));
    const double separation = __gps_nmea_number(__gps_nmea_field(fields, count, 11));
    const double hdop       = __gps_nmea_number(__gps_nmea_field(fields, count, 8));

    gps_handle->fix.latitude    = __gps_nmea_degrees(__gps_nmea_field(fields, count, 2), __gps_nmea_field(fields, count, 3));
    gps_handle->fix.longitude   = __gps_nmea_degrees(__gps_nmea_field(fields, count, 4), __gps_nmea_field(fields, count, 5));
    gps_handle->fix.altMSL      = altitude;
    gps_handle->fix.altHAE      = (isfinite(altitude) && isfinite(separation)) ? altitude + separation : NAN;
    gps_handle->satellites_used = atoi(__gps_nmea_field(fields, count, 7));
    gps_handle->dop.hdop        = isfinite(hdop) ? hdop : gps_handle->gsa_hdop;
    // GSA is authoritative on 2D vs 3D; without it, infer from whether GGA carried an altitude
    gps_handle->fix.mode = (gps_handle->gsa_mode >= MODE_2D) ? gps_handle->gsa_mode : (isfinite(altitude) ? MODE_3D : MODE_2D);
    gps_handle->set      = MODE_SET | LATLON_SET | ALTITUDE_SET;
}

// ------------------------------------------------------------------------------------------------------------------------

// Signatures mirror libgps. "host" is the device path and "port" the baud rate; a non-tty (a captured NMEA
// file or a pipe) is accepted as-is, which makes the parser testable without hardware.
static int gps_open(const char *const host, const char *const port, struct gps_data_t *const gps_handle) {
    memset(gps_handle, 0, sizeof(*gps_handle));
    gps_handle->fix.mode     = MODE_NOT_SEEN;
    gps_handle->fix.latitude = gps_handle->fix.longitude = NAN;
    gps_handle->fix.altMSL = gps_handle->fix.altHAE = NAN;
    gps_handle->dop.hdop                            = NAN;
    gps_handle->gsa_hdop                            = NAN;

    if ((gps_handle->gps_fd = open(host, O_RDONLY | O_NOCTTY | O_NONBLOCK)) < 0)
        return -1;

    struct termios tio;
    if (tcgetattr(gps_handle->gps_fd, &tio) == 0) {
        cfmakeraw(&tio);
        const speed_t baud = __gps_nmea_baud(port);
        (void)cfsetispeed(&tio, baud);
        (void)cfsetospeed(&tio, baud);
        tio.c_cflag |= (CLOCAL | CREAD);
        tio.c_cc[VMIN]  = 0;
        tio.c_cc[VTIME] = 0;
        (void)tcsetattr(gps_handle->gps_fd, TCSANOW, &tio);
    }
    return 0;
}

static int gps_stream(struct gps_data_t *const gps_handle, const unsigned int flags, void *const data) {
    (void)gps_handle;
    (void)flags;
    (void)data;
    return 0; // gpsd's watch protocol has no analogue: the device streams unconditionally
}

static int gps_close(struct gps_data_t *const gps_handle) {
    if (gps_handle->gps_fd >= 0)
        (void)close(gps_handle->gps_fd);
    gps_handle->gps_fd = -1;
    return 0;
}

static int gps_read(struct gps_data_t *const gps_handle, char *const message, const int message_len) {
    (void)message;
    (void)message_len;

    gps_handle->set = 0; // "set" describes this report only, as in libgps

    // A line longer than the buffer can only be corruption: drop it rather than wedging on a full buffer
    if (gps_handle->buffer_length >= sizeof(gps_handle->buffer) - 1)
        gps_handle->buffer_length = 0;

    const ssize_t n = read(gps_handle->gps_fd, gps_handle->buffer + gps_handle->buffer_length, sizeof(gps_handle->buffer) - gps_handle->buffer_length - 1);
    if (n > 0) {
        gps_handle->buffer_length += (size_t)n;
        gps_handle->buffer[gps_handle->buffer_length] = '\0';
    } else if (n < 0 && errno != EAGAIN) // EWOULDBLOCK is EAGAIN on Linux, so testing both would be a tautology
        return (int)n;

    // Consume at most one epoch per call, mirroring libgps' one-report-per-read contract, and leave any
    // further sentences buffered for subsequent calls. A single read can span several epochs (a device that
    // delivers a whole burst in one USB transfer, or a backlog after a stall), and parsing them all here
    // would silently discard every epoch but the last. Parsing continues from the buffer even when this read
    // returned nothing, so a backlog still drains.
    char *begin = gps_handle->buffer, *end;
    while (gps_handle->set == 0 && (end = strpbrk(begin, "\r\n")) != NULL) {
        *end = '\0';
        if (*begin == '$')
            __gps_nmea_sentence(gps_handle, begin);
        begin = end + 1;
    }

    gps_handle->buffer_length -= (size_t)(begin - gps_handle->buffer); // retain any partial trailing sentence
    memmove(gps_handle->buffer, begin, gps_handle->buffer_length);
    gps_handle->buffer[gps_handle->buffer_length] = '\0';

    return (gps_handle->set != 0) ? 1 : (int)((n > 0) ? n : 0);
}

#endif

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------
