#!/bin/sh

cp nas_status.sts /usr/local/bin
cp nas_status.service /etc/systemd/system

systemctl start nas_status
systemctl enable nas_status