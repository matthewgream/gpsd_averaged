Simple mechanism to average gnss positions from gpsd and re-present them on a tcp port in json. For an anchored position, on a consumer grade ublox gnss device, this can quickly stablise to a horizontal uncertainty of ~1.25m and near zero vertically. Included are config defaults and systemd service files (plus those also for gpsd).

```
root@adsb:/opt/gpsd_averaged# ./gpsd_averaged --help
Usage: ./gpsd_averaged [options]
Options:
  -H, --gpsd-host HOST     GPSD host (default 127.0.0.1)
  -P, --gpsd-port PORT     GPSD port (default 2947)
  -p, --port PORT          Client listen port (default 2948)
  -G, --listenany          Client listen on INADDR_ANY (default INADDR_LOOPBACK)
  -f, --filter MODE        Averaging filter: simple, window, kalman (default simple)
  -s, --sats N             Averaging minimum satellites (default 4)
  -h, --hdop HDOP          Averaging maximum HDOP (default 20.0)
  -a, --anchored           Anchored mode, fixed installation
  -i, --interval SECONDS   Interval status (default 1800)
  -b, --background         Background operation
  -v, --verbose            Verbose output
  --help                   This help
root@adsb:/opt/gpsd_averaged# ./gpsd_averaged --filter kalman --interval 15 --anchored
config: gpsd=127.0.0.1:2947, port=2948, filter=kalman, anchored=yes, sats/hdop=4/20.0, listen-any=no, status=15s
status: fixes=15/93, lat=51.50092990, lon=-0.20672488, alt=15.0, stddev_m=0.17/0.22/0.17, window=15, outliers=0, moved=0.02m/h:0.02/v:0.01, conf=0.6m [GATHERING], kalman=lat:4.42e-10/lon:4.42e-10/alt:6.67e+00/unc:3.78m
status: fixes=30/189, lat=51.50092941, lon=-0.20672669, alt=14.8, stddev_m=0.18/0.28/0.21, window=30, outliers=0, moved=0.02m/h:0.02/v:0.01, conf=0.7m [SAMPLING], kalman=lat:2.25e-10/lon:2.25e-10/alt:3.33e+00/unc:2.68m
status: fixes=45/281, lat=51.50092713, lon=-0.20673222, alt=14.7, stddev_m=0.40/0.58/0.21, window=45, outliers=0, moved=0.03m/h:0.03/v:0.00, conf=1.4m [SAMPLING], kalman=lat:1.57e-10/lon:1.57e-10/alt:2.22e+00/unc:2.22m
status: fixes=60/375, lat=51.50092369, lon=-0.20673859, alt=14.7, stddev_m=0.70/0.86/0.21, window=60, outliers=0, moved=0.03m/h:0.03/v:0.01, conf=2.2m [STABILISING], kalman=lat:1.26e-10/lon:1.26e-10/alt:1.67e+00/unc:1.96m
status: fixes=75/469, lat=51.50092030, lon=-0.20674184, alt=14.6, stddev_m=0.90/0.87/0.29, window=75, outliers=0, moved=0.02m/h:0.02/v:0.01, conf=2.5m [STABILISING], kalman=lat:1.09e-10/lon:1.09e-10/alt:1.34e+00/unc:1.79m
status: fixes=90/564, lat=51.50091698, lon=-0.20674136, alt=14.5, stddev_m=1.07/0.81/0.40, window=90, outliers=0, moved=0.02m/h:0.02/v:0.01, conf=2.7m [STABILISING], kalman=lat:9.85e-11/lon:9.85e-11/alt:1.11e+00/unc:1.68m
status: fixes=105/655, lat=51.50091392, lon=-0.20673811, alt=14.2, stddev_m=1.19/0.84/0.70, window=105, outliers=0, moved=0.03m/h:0.02/v:0.02, conf=2.9m [STABILISING], kalman=lat:9.21e-11/lon:9.21e-11/alt:9.56e-01/unc:1.59m
status: fixes=120/750, lat=51.50091195, lon=-0.20673250, alt=14.0, stddev_m=1.20/1.04/0.95, window=120, outliers=0, moved=0.03m/h:0.02/v:0.02, conf=3.2m [STABILISING], kalman=lat:8.80e-11/lon:8.80e-11/alt:8.37e-01/unc:1.53m
status: fixes=135/844, lat=51.50091069, lon=-0.20672738, alt=13.8, stddev_m=1.18/1.18/1.02, window=135, outliers=0, moved=0.02m/h:0.02/v:0.01, conf=3.3m [STABILISING], kalman=lat:8.52e-11/lon:8.52e-11/alt:7.45e-01/unc:1.49m
status: fixes=150/939, lat=51.50090986, lon=-0.20672352, alt=13.7, stddev_m=1.15/1.24/1.00, window=150, outliers=0, moved=0.01m/h:0.01/v:0.00, conf=3.4m [STABILISING], kalman=lat:8.34e-11/lon:8.34e-11/alt:6.72e-01/unc:1.45m
...
status: fixes=2076/13313, lat=51.50089440, lon=-0.20670217, alt=11.4, stddev_m=0.82/2.47/2.69, window=300, outliers=54, moved=0.01m/h:0.00/v:0.01, conf=5.2m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.03e-01/unc:1.21m
status: fixes=2091/13406, lat=51.50089411, lon=-0.20670003, alt=11.3, stddev_m=0.82/2.48/2.75, window=300, outliers=54, moved=0.01m/h:0.00/v:0.01, conf=5.2m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.03e-01/unc:1.21m
status: fixes=2106/13500, lat=51.50089465, lon=-0.20669740, alt=11.2, stddev_m=0.82/2.50/2.82, window=300, outliers=54, moved=0.02m/h:0.01/v:0.01, conf=5.3m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.03e-01/unc:1.21m
status: fixes=2121/13594, lat=51.50089645, lon=-0.20669339, alt=11.1, stddev_m=0.77/2.39/2.75, window=300, outliers=54, moved=0.04m/h:0.03/v:0.02, conf=5.0m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.03e-01/unc:1.21m
status: fixes=2136/13688, lat=51.50089901, lon=-0.20668908, alt=11.1, stddev_m=0.76/2.03/2.39, window=300, outliers=54, moved=0.04m/h:0.03/v:0.02, conf=4.3m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.03e-01/unc:1.21m
status: fixes=2151/13779, lat=51.50090184, lon=-0.20668545, alt=11.0, stddev_m=0.83/1.56/1.83, window=300, outliers=54, moved=0.03m/h:0.03/v:0.02, conf=3.5m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.03e-01/unc:1.21m
status: fixes=2166/13875, lat=51.50090468, lon=-0.20668331, alt=11.0, stddev_m=0.92/1.19/1.42, window=300, outliers=54, moved=0.02m/h:0.02/v:0.01, conf=3.0m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.03e-01/unc:1.21m
status: fixes=2181/13969, lat=51.50090712, lon=-0.20668352, alt=11.0, stddev_m=1.03/1.09/1.45, window=300, outliers=54, moved=0.01m/h:0.01/v:0.01, conf=3.0m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.03e-01/unc:1.21m
status: fixes=2196/14063, lat=51.50090932, lon=-0.20668559, alt=11.0, stddev_m=1.13/1.10/1.64, window=300, outliers=54, moved=0.01m/h:0.01/v:0.01, conf=3.2m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
status: fixes=2211/14155, lat=51.50091091, lon=-0.20668872, alt=11.0, stddev_m=1.20/1.11/1.81, window=300, outliers=54, moved=0.02m/h:0.01/v:0.02, conf=3.3m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
status: fixes=2226/14250, lat=51.50091206, lon=-0.20669212, alt=11.1, stddev_m=1.26/1.08/2.10, window=300, outliers=54, moved=0.03m/h:0.01/v:0.02, conf=3.3m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
status: fixes=2241/14344, lat=51.50091295, lon=-0.20669630, alt=11.1, stddev_m=1.30/1.10/2.49, window=300, outliers=54, moved=0.03m/h:0.01/v:0.03, conf=3.4m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
status: fixes=2256/14438, lat=51.50091388, lon=-0.20670126, alt=11.2, stddev_m=1.34/1.18/2.88, window=300, outliers=54, moved=0.03m/h:0.01/v:0.03, conf=3.6m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
status: fixes=2271/14530, lat=51.50091427, lon=-0.20670521, alt=11.2, stddev_m=1.36/1.26/3.05, window=300, outliers=54, moved=0.02m/h:0.01/v:0.02, conf=3.7m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
status: fixes=2286/14624, lat=51.50091338, lon=-0.20670773, alt=11.2, stddev_m=1.31/1.29/2.95, window=300, outliers=54, moved=0.02m/h:0.01/v:0.02, conf=3.7m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
status: fixes=2301/14717, lat=51.50091189, lon=-0.20671067, alt=11.2, stddev_m=1.19/1.34/2.79, window=300, outliers=54, moved=0.02m/h:0.01/v:0.02, conf=3.6m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
status: fixes=2316/14813, lat=51.50090989, lon=-0.20671469, alt=11.2, stddev_m=1.11/1.43/2.71, window=300, outliers=54, moved=0.02m/h:0.01/v:0.01, conf=3.6m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
status: fixes=2331/14906, lat=51.50090718, lon=-0.20671947, alt=11.3, stddev_m=1.12/1.54/2.75, window=300, outliers=54, moved=0.02m/h:0.01/v:0.01, conf=3.8m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
status: fixes=2346/15000, lat=51.50090375, lon=-0.20672694, alt=11.3, stddev_m=1.20/1.79/2.90, window=300, outliers=54, moved=0.02m/h:0.02/v:0.02, conf=4.3m [STABILISING], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.02e-01/unc:1.21m
...

```
