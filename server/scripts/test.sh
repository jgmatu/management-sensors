# Launch 10 parallel clients, each sending "ping" and exit
#
#!/usr/bin/expect -f

for i in {1..100}; do
  expect -c "
    spawn botan tls_client localhost --port=50443 --trusted-cas=certs/ --policy=./policies/client_policies.txt
    expect \"Handshake complete\"
    send \"ping $i\r\"
    expect \"ping $i\"
    send \"exit\r\"
    expect eof
  " &
done
wait
