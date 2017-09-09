/*
 * common.cpp - the source file of Common class
 *
 * Copyright (C) 2014-2015 Symeon Huang <hzwhuang@gmail.com>
 *
 * This file is part of the libQtShadowsocks.
 *
 * libQtShadowsocks is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * libQtShadowsocks is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libQtShadowsocks; see the file LICENSE. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <QTextStream>
#include <QHostInfo>
#include <QtEndian>
#include <random>
#include <sstream>
#include "common.h"

using namespace QSS;

QTextStream Common::qOut(stdout, QIODevice::WriteOnly | QIODevice::Unbuffered);
QVector<QHostAddress> Common::bannedAddressVector;
QMutex Common::bannedAddressMutex;
const uint8_t Common::ADDRESS_MASK = 0b00001111;//0xf
const uint8_t Common::ONETIMEAUTH_FLAG = 0b00010000;//0x10

const char* Common::version()
{
    return QSS_VERSION;
}

//pack a shadowsocks header
std::string Common::packAddress(const Address &addr, bool auth)
{
    std::string portNs(2, '\0');
    qToBigEndian(addr.getPort(), reinterpret_cast<uchar*>(&portNs[0]));

    std::string addrBin;
    const Address::ATYP type = addr.addressType();
    if (type == Address::HOST) {
        const std::string& addressString = addr.getAddress();
        //can't be longer than 255
        addrBin = static_cast<char>(addressString.length()) + addressString;
    } else if (type == Address::IPV4) {
        uint32_t ipv4Address = qToBigEndian(addr.getFirstIP().toIPv4Address());
        addrBin = std::string(reinterpret_cast<char*>(&ipv4Address), 4);
    } else {
        //Q_IPV6ADDR is a 16-unsigned-char struct (big endian)
        Q_IPV6ADDR ipv6Address = addr.getFirstIP().toIPv6Address();
        addrBin = std::string(reinterpret_cast<char*>(ipv6Address.c), 16);
    }

    char typeChar = static_cast<char>(type);
    if (auth) {
        typeChar |= ONETIMEAUTH_FLAG;
    }

    return typeChar + addrBin + portNs;
}

std::string Common::packAddress(const QHostAddress &addr,
                                const quint16 &port,
                                bool auth)
{
    std::string addrBin;
    char typeChar;
    std::string portNs(2, '\0');
    qToBigEndian(port, reinterpret_cast<uchar*>(&portNs[0]));
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        uint32_t ipv4Address = qToBigEndian(addr.toIPv4Address());
        typeChar = static_cast<char>(Address::IPV4);
        addrBin = std::string(reinterpret_cast<char*>(&ipv4Address), 4);
    } else {
        typeChar = static_cast<char>(Address::IPV6);
        Q_IPV6ADDR ipv6Address = addr.toIPv6Address();
        addrBin = std::string(reinterpret_cast<char*>(ipv6Address.c), 16);
    }
    if (auth) {
        typeChar |= ONETIMEAUTH_FLAG;
    }
    return typeChar + addrBin + portNs;
}

void Common::parseHeader(const std::string &data,
                         Address &dest,
                         int &header_length,
                         bool &authFlag)
{
    char atyp = data[0];
    authFlag |= (atyp & ONETIMEAUTH_FLAG);
    int addrtype = static_cast<int>(atyp & ADDRESS_MASK);
    header_length = 0;

    if (addrtype == Address::HOST) {
        if (data.length() > 2) {
            quint8 addrlen = static_cast<quint8>(data[1]);
            if (data.size() >= 2 + addrlen) {
                dest.setPort(qFromBigEndian(*reinterpret_cast<const quint16 *>
                                            (data.data() + 2 + addrlen))
                             );
                dest.setAddress(data.substr(2, addrlen));
                header_length = 4 + addrlen;
            }
        }
    } else if (addrtype == Address::IPV4) {
        if (data.length() >= 7) {
            QHostAddress addr(qFromBigEndian(*reinterpret_cast<const quint32 *>
                                             (data.data() + 1))
                              );
            if (!addr.isNull()) {
                header_length = 7;
                dest.setIPAddress(addr);
                dest.setPort(qFromBigEndian(*reinterpret_cast<const quint16 *>
                                            (data.data() + 5))
                             );
            }
        }
    } else if (addrtype == Address::IPV6) {
        if (data.length() >= 19) {
            Q_IPV6ADDR ipv6_addr;
            memcpy(ipv6_addr.c, data.data() + 1, 16);
            QHostAddress addr(ipv6_addr);
            if (!addr.isNull()) {
                header_length = 19;
                dest.setIPAddress(addr);
                dest.setPort(qFromBigEndian(*reinterpret_cast<const quint16 *>
                                            (data.data() + 17))
                             );
            }
        }
    }
}

int Common::randomNumber(int max, int min)
{
    std::random_device rd;
    std::default_random_engine engine(rd());
    std::uniform_int_distribution<int> dis(min, max - 1);
    return dis(engine);
}

void Common::exclusive_or(unsigned char *ks,
                          const unsigned char *in,
                          unsigned char *out,
                          quint32 length)
{
    unsigned char *end_ks = ks + length;
    do {
        *out = *in ^ *ks;
        ++out; ++in; ++ks;
    } while (ks < end_ks);
}

void Common::banAddress(const QHostAddress &addr)
{
    bannedAddressMutex.lock();
    bannedAddressVector.append(addr);
    bannedAddressMutex.unlock();
}

bool Common::isAddressBanned(const QHostAddress &addr)
{
    bannedAddressMutex.lock();
    bool banned = bannedAddressVector.contains(addr);
    bannedAddressMutex.unlock();
    return banned;
}

std::string Common::stringFromHex(const std::string& hex)
{
    QByteArray res = QByteArray::fromHex(QByteArray(hex.data(), hex.length()));
    return std::string(res.data(), res.length());
}
