Simple mechanism to average gnss positions from gpsd and re-present them on a tcp port in json. For an anchored position, on a consumer grade ublox gnss device, this can quickly stablise to a planar uncertainty of ~1.25m and near zero vertically. Included are systemd service files (plus those also for gpsd).

```
root@workshop:/opt/gpsd_averaged# ./gpsd_averaged --help
Usage: ./gpsd_averaged [options]
Options:
  -H, --gpsd-host HOST     GPSD host (default 127.0.0.1)
  -P, --gpsd-port PORT     GPSD port (default 2947)
  -p, --port PORT          Client listen port (default 2948)
  -f, --filter MODE        Averaging filter: simple, window, kalman (default simple)
  -s, --sats N             Averaging minimum satellites (default 4)
  -h, --hdop HDOP          Averaging maximum HDOP (default 20.0)
  -a, --anchored           Anchored mode, fixed installation
  -i, --interval SECONDS   Interval status (default 1800)
  -b, --background         Background operation
  -v, --verbose            Verbose output
  --help                   This help
root@workshop:/opt/gpsd_averaged# ./gpsd_averaged --filter kalman --interval 5 --anchored
STATUS: no fixes
STATUS: filter=kalman/anchored, fixes=4/21, lat=51.50091328, lon=-0.20675722, alt=16.1, stddev_m=0.05/0.10/0.01, window=4, outliers=0, move_3d=0.05m (h=0.05m v=0.00m), conf=0.2m [CONVERGING], kalman=lat:1.76e-09/lon:1.76e-09/alt:2.50e+01/uncertainty:7.43m
STATUS: filter=kalman/anchored, fixes=9/46, lat=51.50091212, lon=-0.20675880, alt=16.3, stddev_m=0.15/0.15/0.19, window=9, outliers=0, move_3d=0.06m (h=0.05m v=0.04m), conf=0.4m [CONVERGING], kalman=lat:7.43e-10/lon:7.43e-10/alt:1.11e+01/uncertainty:4.89m
STATUS: filter=kalman/anchored, fixes=14/70, lat=51.50091054, lon=-0.20676220, alt=16.4, stddev_m=0.27/0.35/0.24, window=14, outliers=0, move_3d=0.06m (h=0.06m v=0.02m), conf=0.9m [CONVERGING], kalman=lat:4.73e-10/lon:4.73e-10/alt:7.14e+00/uncertainty:3.91m
STATUS: filter=kalman/anchored, fixes=19/96, lat=51.50090928, lon=-0.20676513, alt=16.5, stddev_m=0.33/0.45/0.21, window=19, outliers=0, move_3d=0.04m (h=0.04m v=0.00m), conf=1.1m [CONVERGING], kalman=lat:3.49e-10/lon:3.49e-10/alt:5.26e+00/uncertainty:3.36m
STATUS: filter=kalman/anchored, fixes=24/121, lat=51.50090835, lon=-0.20676670, alt=16.4, stddev_m=0.35/0.45/0.19, window=24, outliers=0, move_3d=0.02m (h=0.02m v=0.00m), conf=1.1m [CONVERGING], kalman=lat:2.78e-10/lon:2.78e-10/alt:4.17e+00/uncertainty:2.99m
STATUS: filter=kalman/anchored, fixes=29/146, lat=51.50090743, lon=-0.20676693, alt=16.4, stddev_m=0.39/0.41/0.18, window=29, outliers=0, move_3d=0.02m (h=0.02m v=0.00m), conf=1.1m [CONVERGING], kalman=lat:2.32e-10/lon:2.32e-10/alt:3.45e+00/uncertainty:2.73m
STATUS: filter=kalman/anchored, fixes=34/171, lat=51.50090670, lon=-0.20676649, alt=16.4, stddev_m=0.41/0.39/0.16, window=34, outliers=0, move_3d=0.02m (h=0.02m v=0.00m), conf=1.1m [CONVERGED], kalman=lat:2.01e-10/lon:2.01e-10/alt:2.94e+00/uncertainty:2.53m
STATUS: filter=kalman/anchored, fixes=39/196, lat=51.50090609, lon=-0.20676513, alt=16.4, stddev_m=0.42/0.43/0.15, window=39, outliers=0, move_3d=0.02m (h=0.02m v=0.00m), conf=1.2m [CONVERGED], kalman=lat:1.78e-10/lon:1.78e-10/alt:2.57e+00/uncertainty:2.37m
...
STATUS: filter=kalman/anchored, fixes=94/1096, lat=51.50090141, lon=-0.20676498, alt=16.5, stddev_m=0.54/0.63/0.36, window=94, outliers=125, move_3d=0.02m (h=0.01m v=0.02m), conf=1.7m [CONVERGED], kalman=lat:9.65e-11/lon:9.65e-11/alt:1.07e+00/uncertainty:1.65m
STATUS: filter=kalman/anchored, fixes=99/1121, lat=51.50090153, lon=-0.20676435, alt=16.4, stddev_m=0.53/0.63/0.45, window=99, outliers=125, move_3d=0.01m (h=0.01m v=0.01m), conf=1.6m [CONVERGED], kalman=lat:9.44e-11/lon:9.44e-11/alt:1.01e+00/uncertainty:1.62m
STATUS: filter=kalman/anchored, fixes=104/1146, lat=51.50090160, lon=-0.20676402, alt=16.4, stddev_m=0.51/0.62/0.49, window=104, outliers=125, move_3d=0.01m (h=0.00m v=0.01m), conf=1.6m [CONVERGED], kalman=lat:9.25e-11/lon:9.25e-11/alt:9.65e-01/uncertainty:1.60m
STATUS: filter=kalman/anchored, fixes=109/1171, lat=51.50090162, lon=-0.20676395, alt=16.4, stddev_m=0.50/0.61/0.49, window=109, outliers=125, move_3d=0.00m (h=0.00m v=0.00m), conf=1.6m [CONVERGED], kalman=lat:9.08e-11/lon:9.08e-11/alt:9.21e-01/uncertainty:1.58m
STATUS: filter=kalman/anchored, fixes=114/1195, lat=51.50090152, lon=-0.20676424, alt=16.4, stddev_m=0.49/0.60/0.48, window=114, outliers=125, move_3d=0.00m (h=0.00m v=0.00m), conf=1.5m [CONVERGED], kalman=lat:8.94e-11/lon:8.94e-11/alt:8.81e-01/uncertainty:1.56m
STATUS: filter=kalman/anchored, fixes=119/1221, lat=51.50090127, lon=-0.20676493, alt=16.4, stddev_m=0.49/0.60/0.48, window=119, outliers=125, move_3d=0.01m (h=0.01m v=0.00m), conf=1.5m [CONVERGED], kalman=lat:8.82e-11/lon:8.82e-11/alt:8.44e-01/uncertainty:1.54m
STATUS: filter=kalman/anchored, fixes=124/1246, lat=51.50090099, lon=-0.20676544, alt=16.4, stddev_m=0.49/0.60/0.49, window=124, outliers=125, move_3d=0.01m (h=0.01m v=0.01m), conf=1.5m [CONVERGED], kalman=lat:8.71e-11/lon:8.71e-11/alt:8.11e-01/uncertainty:1.52m
STATUS: filter=kalman/anchored, fixes=129/1271, lat=51.50090074, lon=-0.20676577, alt=16.4, stddev_m=0.49/0.59/0.50, window=129, outliers=125, move_3d=0.01m (h=0.00m v=0.01m), conf=1.5m [CONVERGED], kalman=lat:8.62e-11/lon:8.62e-11/alt:7.79e-01/uncertainty:1.50m
STATUS: filter=kalman/anchored, fixes=134/1296, lat=51.50090056, lon=-0.20676578, alt=16.5, stddev_m=0.49/0.58/0.51, window=134, outliers=125, move_3d=0.01m (h=0.00m v=0.01m), conf=1.5m [CONVERGED], kalman=lat:8.54e-11/lon:8.54e-11/alt:7.51e-01/uncertainty:1.49m
STATUS: filter=kalman/anchored, fixes=139/1320, lat=51.50090050, lon=-0.20676551, alt=16.5, stddev_m=0.48/0.57/0.52, window=139, outliers=125, move_3d=0.00m (h=0.00m v=0.00m), conf=1.5m [CONVERGED], kalman=lat:8.47e-11/lon:8.47e-11/alt:7.24e-01/uncertainty:1.48m
...
STATUS: filter=kalman/anchored, fixes=626/4896, lat=51.50089863, lon=-0.20675994, alt=15.0, stddev_m=0.32/1.05/1.35, window=300, outliers=353, move_3d=0.01m (h=0.01m v=0.00m), conf=2.2m [CONVERGED], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.80e-01/uncertainty:1.24m
STATUS: filter=kalman/anchored, fixes=631/4920, lat=51.50089895, lon=-0.20676030, alt=15.0, stddev_m=0.33/1.05/1.37, window=300, outliers=353, move_3d=0.01m (h=0.01m v=0.01m), conf=2.2m [CONVERGED], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.79e-01/uncertainty:1.24m
STATUS: filter=kalman/anchored, fixes=636/4946, lat=51.50089942, lon=-0.20676051, alt=15.0, stddev_m=0.36/1.03/1.37, window=300, outliers=353, move_3d=0.01m (h=0.01m v=0.00m), conf=2.2m [CONVERGED], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.78e-01/uncertainty:1.24m
STATUS: filter=kalman/anchored, fixes=641/4971, lat=51.50090001, lon=-0.20676065, alt=15.0, stddev_m=0.39/1.02/1.38, window=300, outliers=353, move_3d=0.01m (h=0.01m v=0.00m), conf=2.2m [CONVERGED], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.77e-01/uncertainty:1.24m
STATUS: filter=kalman/anchored, fixes=646/4996, lat=51.50090073, lon=-0.20676076, alt=15.0, stddev_m=0.44/1.00/1.38, window=300, outliers=353, move_3d=0.01m (h=0.01m v=0.00m), conf=2.2m [CONVERGED], kalman=lat:7.95e-11/lon:7.95e-11/alt:1.76e-01/uncertainty:1.24m
```
