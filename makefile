
KasaEnergyLogger/usr/local/bin/kasaenergylogger: kasaenergylogger.cpp
	mkdir -p KasaEnergyLogger/usr/local/bin
	g++ -O3 kasaenergylogger.cpp -o KasaEnergyLogger/usr/local/bin/kasaenergylogger

deb: KasaEnergyLogger/usr/local/bin/kasaenergylogger KasaEnergyLogger/DEBIAN/control KasaEnergyLogger/etc/systemd/system/kasaenergylogger.service
	chmod a+x KasaEnergyLogger/DEBIAN/postinst KasaEnergyLogger/DEBIAN/postrm KasaEnergyLogger/DEBIAN/prerm
	dpkg-deb --build KasaEnergyLogger
