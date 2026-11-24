아래 명령어를 수행할 경우 부팅시 자동 실행되게 세팅하는 것

sudo cp ../app/build/oht_controller /usr/bin/
sudo cp oht_controller.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable oht_controller.service
