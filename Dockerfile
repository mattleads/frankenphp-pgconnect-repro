FROM dunglas/frankenphp:1-php8.4
RUN install-php-extensions pdo_pgsql pgsql
COPY Caddyfile /etc/frankenphp/Caddyfile
COPY public /app/public
COPY cli.php /app/cli.php
WORKDIR /app
