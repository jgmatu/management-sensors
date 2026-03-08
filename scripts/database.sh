#!/bin/bash

sudo usermod -aG postgres javi
sudo mkdir -p /usr/local/pgsql/data
sudo chown -R $USER:postgres /usr/local/pgsql/data
sudo chmod 700 /usr/local/pgsql/data

sudo chgrp -R postgres /usr/local/pgsql
# sudo chmod -R g+w /usr/local/pgsql

/usr/local/pgsql/bin/initdb -D /usr/local/pgsql/data
/usr/local/pgsql/bin/pg_ctl -D /usr/local/pgsql/data -l logfile start
/usr/local/pgsql/bin/createdb javi
/usr/local/pgsql/bin/psql -d javi
