<?php
// One unit of work per request, chosen by ?mode=. Prints the mode, the pid and the time spent in microseconds.
$mode = $_GET['mode'] ?? 'pdo';
$host = getenv('PGHOST') ?: 'database';
$t = hrtime(true);
switch ($mode) {
    case 'pdo':   $c = new PDO("pgsql:host=$host;port=5432;dbname=app", 'app', 'app'); $c = null; break;
    case 'pgsql': $c = pg_connect("host=$host port=5432 dbname=app user=app password=app", PGSQL_CONNECT_FORCE_NEW); pg_close($c); break;
    case 'tcp':   [$th, $tp] = explode(':', getenv('TCP_TARGET') ?: "$host:5432"); $s = fsockopen($th, (int) $tp, $errno, $errstr, 5); fclose($s); break;
    case 'sleep': usleep(4000); break;
    case 'ossl':  for ($k = 0; $k < 300; ++$k) { openssl_digest(openssl_random_pseudo_bytes(32), 'sha256'); } break;
    case 'hash':  for ($k = 0; $k < 300; ++$k) { hash('sha256', random_bytes(32)); } break;
    case 'cpu':   $x = 0; for ($i = 0; $i < 300000; ++$i) { $x += $i % 7; } break;
    case 'none':  break;
    default: http_response_code(400); echo "unknown mode\n"; return;
}
header('Content-Type: text/plain');
echo $mode, ' pid=', getmypid(), ' ', (int) ((hrtime(true) - $t) / 1000), "us\n";
