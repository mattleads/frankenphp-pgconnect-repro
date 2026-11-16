<?php
// Same unit of work as public/index.php, repeated $argv[2] times in one process. Prints "<mode> <count> <seconds>".
$mode = $argv[1] ?? 'pdo';
$n = (int) ($argv[2] ?? 200);
$host = getenv('PGHOST') ?: 'database';
$t = hrtime(true);
for ($i = 0; $i < $n; ++$i) {
    switch ($mode) {
        case 'pdo':   $c = new PDO("pgsql:host=$host;port=5432;dbname=app", 'app', 'app'); $c = null; break;
        case 'pgsql': $c = pg_connect("host=$host port=5432 dbname=app user=app password=app", PGSQL_CONNECT_FORCE_NEW); pg_close($c); break;
        case 'tcp':   [$th, $tp] = explode(':', getenv('TCP_TARGET') ?: "$host:5432"); $s = fsockopen($th, (int) $tp, $errno, $errstr, 5); fclose($s); break;
        case 'sleep': usleep(4000); break;
    case 'ossl':  for ($k = 0; $k < 300; ++$k) { openssl_digest(openssl_random_pseudo_bytes(32), 'sha256'); } break;
    case 'hash':  for ($k = 0; $k < 300; ++$k) { hash('sha256', random_bytes(32)); } break;
        case 'cpu':   $x = 0; for ($j = 0; $j < 300000; ++$j) { $x += $j % 7; } break;
    }
}
printf("%s %d %.3f\n", $mode, $n, (hrtime(true) - $t) / 1e9);
