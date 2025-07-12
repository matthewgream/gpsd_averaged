
// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

/*
 * gpsd_averaged - GPS position averaging daemon
 * Reads from gpsd via socket, provides averaged positions via JSON socket
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <gps.h>

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

#define WINDOW_SIZE 300           // Keep last 5 minutes at 1Hz
#define OUTLIER_THRESHOLD 3.0     // Reject if > 3 standard deviations
#define KALMAN_PROCESS_NOISE 0.1  // Process noise for Kalman filter
#define KALMAN_MEASURE_NOISE 25.0 // Measurement noise in meters

typedef enum { AVERAGE_FILTER_SIMPLE, AVERAGE_FILTER_WINDOW, AVERAGE_FILTER_KALMAN } average_filter_t;

#define BUFFER_MAX 1024

static bool verbose = false;

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

#define DEFAULT_GPSD_HOST "127.0.0.1"
#ifndef DEFAULT_GPSD_PORT
#define DEFAULT_GPSD_PORT "2947"
#endif
#define DEFAULT_PORT (2947 + 1)
#define DEFAULT_FILTER AVERAGE_FILTER_SIMPLE
#define DEFAULT_HDOP_MAX 20.0
#define DEFAULT_SATELLITES_MIN 4
#define DEFAULT_ANCHORED false
#define DEFAULT_INTERVAL_STATUS (30 * 60)

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

static bool interval_passed(time_t *const last, const time_t interval) {
    const time_t now = time(NULL);
    if (interval > 0 && (now - *last) >= interval) {
        *last = now;
        return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

typedef struct {
    double lat, lon, alt;
    time_t timestamp;
} sample_t;
typedef struct {
    sample_t samples[WINDOW_SIZE];
    int head, size;
} sliding_window_t;

static void window_add(sliding_window_t *const w, const double lat, const double lon, const double alt) {
    w->samples[w->head].lat       = lat;
    w->samples[w->head].lon       = lon;
    w->samples[w->head].alt       = alt;
    w->samples[w->head].timestamp = time(NULL);
    w->head                       = (w->head + 1) % WINDOW_SIZE;
    w->size                       = w->size + (w->size < WINDOW_SIZE ? 1 : 0);
}

static void window_get_stats(const sliding_window_t *const w, double *const mean_lat, double *const mean_lon, double *const mean_alt, double *const stddev_lat,
                             double *const stddev_lon, double *const stddev_alt) {
    if (w->size == 0)
        return;
    double sum_lat = 0, sum_lon = 0, sum_alt = 0;
    double sum_sq_lat = 0, sum_sq_lon = 0, sum_sq_alt = 0;
    for (int i = 0; i < w->size; i++) {
        sum_lat += w->samples[i].lat;
        sum_lon += w->samples[i].lon;
        sum_alt += w->samples[i].alt;
        sum_sq_lat += w->samples[i].lat * w->samples[i].lat;
        sum_sq_lon += w->samples[i].lon * w->samples[i].lon;
        sum_sq_alt += w->samples[i].alt * w->samples[i].alt;
    }
    *mean_lat   = sum_lat / w->size;
    *mean_lon   = sum_lon / w->size;
    *mean_alt   = sum_alt / w->size;
    *stddev_lat = sqrt((sum_sq_lat / w->size) - (*mean_lat * *mean_lat));
    *stddev_lon = sqrt((sum_sq_lon / w->size) - (*mean_lon * *mean_lon));
    *stddev_alt = sqrt((sum_sq_alt / w->size) - (*mean_alt * *mean_alt));
}

// ------------------------------------------------------------------------------------------------------------------------

typedef struct {
    double estimate;
    double error_covariance;
    double process_noise;
    double measure_noise;
} kalman_state_t;

static void kalman_init(kalman_state_t *const k, const double initial_estimate, const double initial_error) {
    k->estimate         = initial_estimate;
    k->error_covariance = initial_error * initial_error; // Convert to variance
    k->process_noise    = KALMAN_PROCESS_NOISE;
    k->measure_noise    = KALMAN_MEASURE_NOISE;
}

static double kalman_update(kalman_state_t *const k, const double measurement) {
    k->error_covariance += k->process_noise;
    const double kalman_gain = k->error_covariance / (k->error_covariance + k->measure_noise);
    k->estimate += kalman_gain * (measurement - k->estimate);
    k->error_covariance = (1.0 - kalman_gain) * k->error_covariance; // This line was wrong
    return k->estimate;
}

// ------------------------------------------------------------------------------------------------------------------------

typedef struct {
    double lat_sum, lon_sum, alt_sum;
    double lat_sum_sq, lon_sum_sq, alt_sum_sq;
    unsigned long count;
    time_t first_fix, last_fix;
    double latitude, longitude, altitude;
    double lat_variance, lon_variance, alt_variance;
    unsigned long received_fixes, rejected_fixes;
    unsigned long outliers_rejected;
    average_filter_t filter;
    bool anchored;
    sliding_window_t window;
    kalman_state_t kalman_lat, kalman_lon, kalman_alt;
    // Convergence tracking
    double last_lat, last_lon, last_alt;
    double pos_change_m, alt_change_m;
    double variance_trend;
    time_t convergence_start;
    bool is_converged;
} average_state_t;

static double calculate_position_change_meters(const double lat1, const double lon1, const double lat2, const double lon2) {
    const double dlat = (lat2 - lat1) * 111320.0, dlon = (lon2 - lon1) * 111320.0 * cos(lat1 * M_PI / 180.0);
    return sqrt(dlat * dlat + dlon * dlon);
}

static void average_begin(average_state_t *const state, const average_filter_t filter, const bool anchored) {
    *state                             = (average_state_t){ 0 };
    state->filter                      = filter;
    state->anchored                    = anchored;
    state->kalman_lat.error_covariance = 100.0; // Large initial uncertainty
    state->kalman_lon.error_covariance = 100.0;
    state->kalman_alt.error_covariance = 100.0;
}

static void average_update(average_state_t *const state, const double lat, const double lon, const double alt) {

    if (state->window.size >= 10) {
        double mean_lat, mean_lon, mean_alt, stddev_lat, stddev_lon, stddev_alt;
        window_get_stats(&state->window, &mean_lat, &mean_lon, &mean_alt, &stddev_lat, &stddev_lon, &stddev_alt);
        const double lat_diff = fabs(lat - mean_lat), lon_diff = fabs(lon - mean_lon), alt_diff = fabs(alt - mean_alt);
        if (state->anchored && state->count > 100) { // After 100 samples
            const double distance_m = calculate_position_change_meters(mean_lat, mean_lon, lat, lon);
            // Anchored can't move more than 2 meters!
            if (distance_m > 2.0 || alt_diff > 3.0) {
                state->outliers_rejected++;
                if (verbose)
                    printf("Anchored mode outlier: %.8f,%.8f,%.1f (%.1fm away)\n", lat, lon, alt, distance_m);
                return;
            }
        } else {
            const double MIN_STDDEV_POS = 0.00001, MIN_STDDEV_ALT = 0.5;
            if (stddev_lat < MIN_STDDEV_POS)
                stddev_lat = MIN_STDDEV_POS;
            if (stddev_lon < MIN_STDDEV_POS)
                stddev_lon = MIN_STDDEV_POS;
            if (stddev_alt < MIN_STDDEV_ALT)
                stddev_alt = MIN_STDDEV_ALT;
            if (lat_diff > OUTLIER_THRESHOLD * stddev_lat || lon_diff > OUTLIER_THRESHOLD * stddev_lon || alt_diff > OUTLIER_THRESHOLD * stddev_alt) {
                state->outliers_rejected++;
                if (verbose)
                    printf("Outlier rejected: %.8f,%.8f,%.1f (%.1f/%.1f/%.1f stddevs)\n", lat, lon, alt, lat_diff / stddev_lat, lon_diff / stddev_lon, alt_diff / stddev_alt);
                return;
            }
        }
    }

    window_add(&state->window, lat, lon, alt);

    if (state->count == 0) {
        kalman_init(&state->kalman_lat, lat, 0.0001);
        kalman_init(&state->kalman_lon, lon, 0.0001);
        kalman_init(&state->kalman_alt, alt, 10.0);
        if (state->anchored) {
            state->kalman_lat.measure_noise = 0.00008 * 0.00008;
            state->kalman_lon.measure_noise = 0.00008 * 0.00008;
            state->kalman_alt.measure_noise = 100.0;
            state->kalman_lat.process_noise = 1e-12; // Essentially zero
            state->kalman_lon.process_noise = 1e-12;
            state->kalman_alt.process_noise = 0.0001; // Tiny bit for pressure changes
        } else {
            state->kalman_lat.measure_noise = 0.00008 * 0.00008;
            state->kalman_lon.measure_noise = 0.00008 * 0.00008;
            state->kalman_alt.measure_noise = 100.0;
            state->kalman_lat.process_noise = 0.000000001;
            state->kalman_lon.process_noise = 0.000000001;
            state->kalman_alt.process_noise = 0.01;
        }
    } else {
        kalman_update(&state->kalman_lat, lat);
        kalman_update(&state->kalman_lon, lon);
        kalman_update(&state->kalman_alt, alt);
    }

    state->count++;

    const int samples = (state->window.size < WINDOW_SIZE) ? state->window.size : WINDOW_SIZE;
    double sum_lat = 0, sum_lon = 0, sum_alt = 0;
    for (int i = 0; i < samples; i++) {
        sum_lat += state->window.samples[i].lat;
        sum_lon += state->window.samples[i].lon;
        sum_alt += state->window.samples[i].alt;
    }
    state->latitude  = sum_lat / samples;
    state->longitude = sum_lon / samples;
    state->altitude  = sum_alt / samples;

    double var_lat = 0, var_lon = 0, var_alt = 0;
    for (int i = 0; i < samples; i++) {
        var_lat += (state->window.samples[i].lat - state->latitude) * (state->window.samples[i].lat - state->latitude);
        var_lon += (state->window.samples[i].lon - state->longitude) * (state->window.samples[i].lon - state->longitude);
        var_alt += (state->window.samples[i].alt - state->altitude) * (state->window.samples[i].alt - state->altitude);
    }

    state->lat_variance = var_lat / samples;
    state->lon_variance = var_lon / samples;
    state->alt_variance = var_alt / samples;

    if (state->lat_variance < 0)
        state->lat_variance = 0;
    if (state->lon_variance < 0)
        state->lon_variance = 0;
    if (state->alt_variance < 0)
        state->alt_variance = 0;

    state->last_fix = time(NULL);
    if (state->first_fix == 0)
        state->first_fix = state->last_fix;

    if (state->count > 1) {
        state->pos_change_m = calculate_position_change_meters(state->last_lat, state->last_lon, state->latitude, state->longitude);
        state->alt_change_m = fabs(state->altitude - state->last_alt);
    }
    state->last_lat = state->latitude;
    state->last_lon = state->longitude;
    state->last_alt = state->altitude;
    if (state->pos_change_m < 0.5 && !state->is_converged) {
        if (state->convergence_start == 0)
            state->convergence_start = time(NULL);
        else if (time(NULL) - state->convergence_start > 30)
            state->is_converged = true;
    } else if (state->pos_change_m >= 0.5) {
        state->convergence_start = 0;
        state->is_converged      = false;
    }
}

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

int gps_satellites_min = DEFAULT_SATELLITES_MIN;
double gps_hdop_max    = DEFAULT_HDOP_MAX;

static bool gps_process_fix_is_quality_acceptable(const struct gps_data_t *const gps_handle) {
    return (gps_handle->satellites_used >= gps_satellites_min && gps_handle->dop.hdop <= gps_hdop_max);
}

static void gps_process_fix(const struct gps_data_t *const gps_handle, average_state_t *const state) {
    state->received_fixes++;
    if (gps_process_fix_is_quality_acceptable(gps_handle)) {
        average_update(state, gps_handle->fix.latitude, gps_handle->fix.longitude, gps_handle->fix.altitude);
        if (verbose)
            printf("Fix %lu: %.8f,%.8f,%.1f sats=%d hdop=%.1f\n", state->count, gps_handle->fix.latitude, gps_handle->fix.longitude, gps_handle->fix.altitude,
                   gps_handle->satellites_used, gps_handle->dop.hdop);
    } else {
        state->rejected_fixes++;
        if (verbose)
            printf("Fix rejected: sats=%d hdop=%.1f\n", gps_handle->satellites_used, gps_handle->dop.hdop);
    }
}

static bool gps_connect(struct gps_data_t *const gps_handle, const char *const gpsd_host, const char *const gpsd_port, const int satellites_min, const double hdop_max) {
    gps_satellites_min = satellites_min;
    gps_hdop_max       = hdop_max;
    if (gps_open(gpsd_host, gpsd_port, gps_handle) != 0) {
        fprintf(stderr, "Failed to connect to gpsd at %s:%s\n", gpsd_host, gpsd_port);
        return false;
    }
    gps_stream(gps_handle, WATCH_ENABLE | WATCH_JSON, NULL);
    fcntl(gps_handle->gps_fd, F_SETFL, fcntl(gps_handle->gps_fd, F_GETFL, 0) | O_NONBLOCK);
    return true;
}

static void gps_disconnect(struct gps_data_t *const gps_handle) {
    gps_stream(gps_handle, WATCH_DISABLE, NULL);
    gps_close(gps_handle);
}

static void gps_process(struct gps_data_t *const gps_handle, average_state_t *const state) {
    if (gps_read(gps_handle, NULL, 0) > 0)
        if (gps_handle->set & MODE_SET && gps_handle->fix.mode >= MODE_2D)
            gps_process_fix(gps_handle, state);
}

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

static void client_format_error_response(char *const buf, const size_t buflen, const char *const message) {
    snprintf(buf, buflen, "{\"class\":\"ERROR\",\"message\":\"%s\"}\r\n", message);
}

static void client_format_version_response(char *const buf, const size_t buflen) { snprintf(buf, buflen, "{\"class\":\"VERSION\",\"release\":\"gpsd_averaged 1.0\"}\r\n"); }

static void client_format_stats_response(char *const buf, const size_t buflen, const average_state_t *const state) {
    if (state->count > 0)
        snprintf(buf, buflen,
                 "{\"class\":\"STATS\","
                 "\"samples\":%lu,\"rejected\":%lu,"
                 "\"first_fix\":%ld,\"last_fix\":%ld,"
                 "\"lat_stddev\":%.6f,\"lon_stddev\":%.6f,\"alt_stddev\":%.2f}\r\n",
                 state->count, state->rejected_fixes, state->first_fix, state->last_fix, sqrt(state->lat_variance), sqrt(state->lon_variance), sqrt(state->alt_variance));
    else
        client_format_error_response(buf, buflen, "No statistics available");
}

static void client_format_json_response(char *const buf, const size_t buflen, const average_state_t *const state) {
    if (state->count > 0) {
        const double lat = (state->filter == AVERAGE_FILTER_KALMAN) ? state->kalman_lat.estimate : state->latitude,
                     lon = (state->filter == AVERAGE_FILTER_KALMAN) ? state->kalman_lon.estimate : state->longitude,
                     alt = (state->filter == AVERAGE_FILTER_KALMAN) ? state->kalman_alt.estimate : state->altitude;
        snprintf(buf, buflen,
                 "{\"class\":\"TPV\",\"device\":\"averaged\",\"mode\":3,"
                 "\"lat\":%.8f,\"lon\":%.8f,\"alt\":%.2f,"
                 "\"samples\":%lu,\"window\":%d,\"outliers\":%lu,"
                 "\"lat_err\":%.2f,\"lon_err\":%.2f,\"alt_err\":%.2f,\"age\":%ld}\r\n",
                 lat, lon, alt, state->count, state->window.size, state->outliers_rejected, sqrt(state->lat_variance) * 111320.0,
                 sqrt(state->lon_variance) * 111320.0 * cos(lat * M_PI / 180.0), sqrt(state->alt_variance), time(NULL) - state->last_fix);
    } else
        client_format_error_response(buf, buflen, "No positions available");
}

static bool client_start(int *const client_listen_fd, const unsigned short port) {
    if ((*client_listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return false;
    }

    const int yes = 1;
    setsockopt(*client_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(port);

    if (bind(*client_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(*client_listen_fd);
        return false;
    }

    if (listen(*client_listen_fd, 5) < 0) {
        perror("listen");
        close(*client_listen_fd);
        return false;
    }

    fcntl(*client_listen_fd, F_SETFL, fcntl(*client_listen_fd, F_GETFL, 0) | O_NONBLOCK);

    return true;
}

static void client_stop(int *const client_listen_fd) {
    if (*client_listen_fd >= 0) {
        close(*client_listen_fd);
        *client_listen_fd = -1;
    }
}

static void client_handle(const int client_fd, const average_state_t *const state) {
    char request[BUFFER_MAX], response[BUFFER_MAX];
    const ssize_t n = recv(client_fd, request, sizeof(request) - 1, MSG_DONTWAIT);
    if (n > 0) {
        request[n] = '\0';
        if (strstr(request, "?WATCH") || strstr(request, "?POLL"))
            client_format_json_response(response, sizeof(response), state);
        else if (strstr(request, "?VERSION"))
            client_format_version_response(response, sizeof(response));
        else if (strstr(request, "?STATS"))
            client_format_stats_response(response, sizeof(response), state);
        else
            client_format_error_response(response, sizeof(response), "Unknown request");
    } else
        client_format_json_response(response, sizeof(response), state);
    send(client_fd, response, strlen(response), MSG_NOSIGNAL);
    close(client_fd);
}

static void client_process(const int *const client_listen_fd, const average_state_t *const state) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    const int client_fd  = accept(*client_listen_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd >= 0)
        client_handle(client_fd, state);
}

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

static volatile bool process_running = true;

static void process_signal(const int sig __attribute__((unused))) { process_running = false; }

static void process_status(const average_state_t *const average_state) {
    if (average_state->count == 0) {
        printf("STATUS: no fixes\n");
        return;
    }

    double lat, lon, alt;
    double uncertainty_m;
    const char *filter_name;
    double lat_unc_m, lon_unc_m, alt_unc_m;

    switch (average_state->filter) {
    case AVERAGE_FILTER_KALMAN:
        lat           = average_state->kalman_lat.estimate;
        lon           = average_state->kalman_lon.estimate;
        alt           = average_state->kalman_alt.estimate;
        lat_unc_m     = sqrt(average_state->kalman_lat.error_covariance) * 111320.0;
        lon_unc_m     = sqrt(average_state->kalman_lon.error_covariance) * 111320.0 * cos(lat * M_PI / 180.0);
        alt_unc_m     = sqrt(average_state->kalman_alt.error_covariance);
        uncertainty_m = sqrt(lat_unc_m * lat_unc_m + lon_unc_m * lon_unc_m + alt_unc_m * alt_unc_m);
        filter_name   = "kalman";
        break;
    case AVERAGE_FILTER_WINDOW:
    case AVERAGE_FILTER_SIMPLE:
    default:
        lat           = average_state->latitude;
        lon           = average_state->longitude;
        alt           = average_state->altitude;
        uncertainty_m = 0;
        filter_name   = (average_state->filter == AVERAGE_FILTER_WINDOW) ? "window" : "simple";
        break;
    }

    const double lat_stddev          = sqrt(average_state->lat_variance);
    const double lon_stddev          = sqrt(average_state->lon_variance);
    const double alt_stddev          = sqrt(average_state->alt_variance);
    const double lat_error_m         = lat_stddev * 111320.0;
    const double lon_error_m         = lon_stddev * 111320.0 * cos(lat * M_PI / 180.0);
    const double confidence_radius_m = 2.0 * sqrt(lat_error_m * lat_error_m + lon_error_m * lon_error_m);
    const double movement_3d         = sqrt(average_state->pos_change_m * average_state->pos_change_m + average_state->alt_change_m * average_state->alt_change_m);

    printf("STATUS: filter=%s%s, fixes=%lu/%lu, lat=%.8f, lon=%.8f, alt=%.1f, stddev_m=%.2f/%.2f/%.2f, window=%d, outliers=%lu, move_3d=%.2fm (h=%.2fm v=%.2fm), conf=%.1fm%s",
           filter_name, average_state->anchored ? "/anchored" : "", average_state->count, average_state->received_fixes, lat, lon, alt, lat_error_m, lon_error_m, alt_stddev,
           average_state->window.size, average_state->outliers_rejected, movement_3d, average_state->pos_change_m, average_state->alt_change_m, confidence_radius_m,
           average_state->is_converged ? " [CONVERGED]" : (average_state->pos_change_m < 0.5 ? " [CONVERGING]" : ""));
    if (average_state->filter == AVERAGE_FILTER_KALMAN)
        printf(", kalman=lat:%.2e/lon:%.2e/alt:%.2e/uncertainty:%.2fm", average_state->kalman_lat.error_covariance, average_state->kalman_lon.error_covariance,
               average_state->kalman_alt.error_covariance, uncertainty_m);
    printf ("\n");
    fflush(stdout);
}

static void process_loop(struct gps_data_t *const gps_handle, const int *const client_listen_fd, average_state_t *const average_state, const time_t interval_status) {
    time_t last_status = 0;

    signal(SIGINT, process_signal);
    signal(SIGTERM, process_signal);
    signal(SIGPIPE, SIG_IGN);

    while (process_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(gps_handle->gps_fd, &rfds);
        FD_SET(*client_listen_fd, &rfds);
        const int maxfd   = (gps_handle->gps_fd > *client_listen_fd) ? gps_handle->gps_fd : *client_listen_fd;
        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
        if (select(maxfd + 1, &rfds, NULL, NULL, &tv) < 0) {
            if (errno != EINTR) {
                perror("select");
                break;
            }
            continue;
        }

        if (FD_ISSET(gps_handle->gps_fd, &rfds))
            gps_process(gps_handle, average_state);

        if (FD_ISSET(*client_listen_fd, &rfds))
            client_process(client_listen_fd, average_state);

        if (interval_passed(&last_status, interval_status))
            process_status(average_state);
    }
}

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

typedef struct {
    const char *gpsd_host, *gpsd_port;
    int port;
    average_filter_t filter;
    int satellites_min;
    double hdop_max;
    bool anchored;
    int interval_status;
    bool verbose;
    bool daemon;
} config_t;

static const struct option options[] = { // defaults
    { "gpsd-host", required_argument, 0, 'H' }, { "gpsd-port", required_argument, 0, 'P' }, { "port", required_argument, 0, 'p' }, { "filter", required_argument, 0, 'f' },
    { "sats", required_argument, 0, 's' },      { "hdop", required_argument, 0, 'h' },      { "anchored", no_argument, 0, 'a' },   { "interval", required_argument, 0, 'i' },
    { "background", no_argument, 0, 'b' },      { "verbose", no_argument, 0, 'v' },         { "help", no_argument, 0, '?' },       { 0, 0, 0, 0 }
};

static void usage(const char *const prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -H, --gpsd-host HOST     GPSD host (default %s)\n", DEFAULT_GPSD_HOST);
    printf("  -P, --gpsd-port PORT     GPSD port (default %s)\n", DEFAULT_GPSD_PORT);
    printf("  -p, --port PORT          Client listen port (default %d)\n", DEFAULT_PORT);
    printf("  -f, --filter MODE        Averaging filter: simple, window, kalman (default simple)\n");
    printf("  -s, --sats N             Averaging minimum satellites (default %d)\n", DEFAULT_SATELLITES_MIN);
    printf("  -h, --hdop HDOP          Averaging maximum HDOP (default %.1f)\n", DEFAULT_HDOP_MAX);
    printf("  -a, --anchored           Anchored mode, fixed installation\n");
    printf("  -i, --interval SECONDS   Interval status (default %d)\n", DEFAULT_INTERVAL_STATUS);
    printf("  -b, --background         Background operation\n");
    printf("  -v, --verbose            Verbose output\n");
    printf("  --help                   This help\n");
}

static int parse_arguments(const int argc, char *const argv[], config_t *const config) {
    int opt;
    while ((opt = getopt_long(argc, argv, "H:P:p:f:s:h:ai:bv?", options, NULL)) != -1)
        switch (opt) {
        case 'H':
            config->gpsd_host = optarg;
            break;
        case 'P':
            config->gpsd_port = optarg;
            break;
        case 'p':
            config->port = atoi(optarg);
            break;
        case 'f':
            if (strcmp(optarg, "kalman") == 0)
                config->filter = AVERAGE_FILTER_KALMAN;
            else if (strcmp(optarg, "window") == 0)
                config->filter = AVERAGE_FILTER_WINDOW;
            else
                config->filter = AVERAGE_FILTER_SIMPLE;
            break;
        case 's':
            config->satellites_min = atoi(optarg);
            break;
        case 'h':
            config->hdop_max = atof(optarg);
            break;
        case 'a':
            config->anchored = true;
            break;
        case 'i':
            config->interval_status = atoi(optarg);
            break;
        case 'b':
            config->daemon = true;
            break;
        case 'v':
            config->verbose = true;
            break;
        case '?':
        default:
            usage(argv[0]);
            return -1;
        }
    return 0;
}

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------

static config_t config = { // defaults
    .gpsd_host       = DEFAULT_GPSD_HOST,
    .gpsd_port       = DEFAULT_GPSD_PORT,
    .port            = DEFAULT_PORT,
    .filter          = DEFAULT_FILTER,
    .satellites_min  = DEFAULT_SATELLITES_MIN,
    .hdop_max        = DEFAULT_HDOP_MAX,
    .anchored        = DEFAULT_ANCHORED,
    .interval_status = DEFAULT_INTERVAL_STATUS,
    .verbose         = false,
    .daemon          = false
};

static struct gps_data_t gps_handle;
static average_state_t average_state;
static int client_listen_fd;

int main(const int argc, char *const argv[]) {

    if (parse_arguments(argc, argv, &config) < 0)
        exit(EXIT_SUCCESS);

    verbose = config.verbose;

    if (config.daemon && daemon(0, 0) < 0) {
        perror("daemon");
        exit(EXIT_FAILURE);
    }

    if (!gps_connect(&gps_handle, config.gpsd_host, config.gpsd_port, config.satellites_min, config.hdop_max))
        exit(EXIT_FAILURE);
    if (!client_start(&client_listen_fd, (unsigned short)config.port)) {
        gps_disconnect(&gps_handle);
        exit(EXIT_FAILURE);
    }
    average_begin(&average_state, config.filter, config.anchored);
    if (config.verbose)
        printf("gpsd_averaged started on port %d, connected to gpsd at %s:%s\n", config.port, config.gpsd_host, config.gpsd_port);

    process_loop(&gps_handle, &client_listen_fd, &average_state, config.interval_status);

    client_stop(&client_listen_fd);
    gps_disconnect(&gps_handle);

    return 0;
}

// ------------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------------------------------
