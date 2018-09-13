/*
 * Copyright (c) 2018 Kaan-Sat <https://kaansat.com.mx/>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "crc32.h"
#include "Constants.h"
#include "DataParser.h"
#include "SerialManager.h"

/**
 * Generates a vector of 0-value @c QVariant with @c x elements, where @c x
 * is the number of data/readings/status variables sent by the CanSat
 */
static QVector<QVariant> EmptyDataPacket() {
    QVector<QVariant> packet;
    const int packetItems = static_cast<int>(kChecksumCode);

    for (int i = 0; i < packetItems; ++i)
        packet.append(QVariant (0));

    return packet;
}

/**
 * Class constructor function, initializes private members and configures
 * SIGNALS/SLOTS between the @c SerialManager class and data handling slots.
 */
DataParser::DataParser() :
    m_crc32(0),
    m_data(EmptyDataPacket()),
    m_csvLoggingEnabled (false)
{
    connect (SerialManager::getInstance(), &SerialManager::packetReceived,
             this, &DataParser::parsePacket);
    connect (this, &DataParser::dataParsed,
             this, &DataParser::saveCsvData);
}

/**
 * @returns the team ID number (extracted from the last packet received)
 */
int DataParser::teamId() const {
    return m_data.at(kTeamID).toInt();
}

/**
 * @returns the packet ID (extracted from the last packet received)
 */
int DataParser::packetCount() const {
    return m_data.at(kPacketCount).toInt();
}

/**
 * @returns the mission time in milliseconds (extracted from the last packet
 *          received)
 */
quint64 DataParser::missionTime() const {
    return m_data.at(kMisionTime).toUInt();
}

/**
 * @returns the altitude of the CanSat in meters (extracted from the last
 *          packet received)
 */
double DataParser::altitude() const {
    return m_data.at(kAltitude).toDouble();
}

/**
 * @returns the battery voltage of the CanSat (extracted from the last packet
 *          received)
 */
double DataParser::batteryVoltage() const {
    return m_data.at(kBatteryVoltage).toDouble();
}

double DataParser::internalTemperature() const {
    return m_data.at(kInternalTemp).toDouble();
}

double DataParser::externalTemperature() const {
    return m_data.at(kExternalTemp).toDouble();
}

double DataParser::atmosphericPressure() const {
    return m_data.at(kAtmPressure).toDouble();
}

QDateTime DataParser::gpsTime() const {
    return m_data.at(kGpsTime).toDateTime();
}

double DataParser::gpsVelocity() const {
    return m_data.at(kGpsVelocity).toDouble();
}

double DataParser::gpsAltitude() const {
    return m_data.at(kGpsAltitude).toDouble();
}

double DataParser::gpsLatitude() const {
    return m_data.at(kGpsLatitude).toDouble();
}

double DataParser::gpsLongitude() const {
    return m_data.at(kGpsLongitude).toDouble();
}

int DataParser::gpsSatelliteCount() const {
    return m_data.at(kGpsSatelliteCount).toInt();
}


QVector3D DataParser::gyroscopeData() const {
    QVector3D vector;
    vector.setX(m_data.at(kGyroscopeX).toFloat());
    vector.setY(m_data.at(kGyroscopeY).toFloat());
    vector.setZ(m_data.at(kGyroscopeZ).toFloat());
    return vector;
}

QVector3D DataParser::accelerometerData() const {
    QVector3D vector;
    vector.setX(m_data.at(kAccelerometerX).toFloat());
    vector.setY(m_data.at(kAccelerometerY).toFloat());
    vector.setZ(m_data.at(kAccelerometerZ).toFloat());
    return vector;
}

quint32 DataParser::checksum() const {
    return m_data.at(kChecksumCode).toUInt();
}

bool DataParser::csvLoggingEnabled() const {
    return m_csvLoggingEnabled;
}

void DataParser::enableCsvLogging(const bool enabled) {
    m_csvLoggingEnabled = enabled;
    emit csvLoggingEnabledChanged();
}

void DataParser::parsePacket(const QByteArray& packet) {
    // Define 'global' function variables
    QStringList data;

    //--------------------------------------------------------------------------
    // Raw packet validation (so that we don't crash while reading data)
    //--------------------------------------------------------------------------
    {
        // Packet is empty, abort
        if (packet.isEmpty()) {
            emit packetError();
            return;
        }

        // Packet does not begin with header code, abort
        if (!packet.startsWith(HEADER_CODE)) {
            emit packetError();
            return;
        }

        // Packet does not end with secondary EOT code (primary EOT code was
        // used to separate incoming packets
        if (!packet.endsWith(EOT_SECONDARY.toLatin1())) {
            emit packetError();
            return;
        }

        // Ok, we can now begin analyzing the packet, start by making a copy
        // of the packet so that we can manipulate it
        QString copy = packet;

        // Remove secondary EOT character, we do not need it
        copy.chop(1);

        // Split packet data and verify that its length is valid
        data = copy.split(DATA_SEPARATOR);
        if (data.count() != EmptyDataPacket().count()) {
            emit packetError();
            return;
        }
    }

    //--------------------------------------------------------------------------
    // CRC-32 validation
    //--------------------------------------------------------------------------
    {
        // Re-construct packet without CRC32 code
        QString rp;
        for (int i = 0; i < EmptyDataPacket().size(); ++i) {
            if (i != kChecksumCode) {
                rp.append(data.at(i));
                rp.append(DATA_SEPARATOR);
            }
        }

        // Calculate CRC-32 from reconstructed packet
        const size_t buffer_size = static_cast<size_t>(rp.size());
        const void* buffer = static_cast<const void*>(rp.toStdString().c_str());
        quint32 localCrc32 = CRC32(buffer, buffer_size);

        // Compare remote and local CRC-32 codes
        quint32 remoteCrc32 = data.at(kChecksumCode).toUInt();
        if (localCrc32 != remoteCrc32) {
            emit packetError();
            return;
        }
    }

    //--------------------------------------------------------------------------
    // Data handling
    //--------------------------------------------------------------------------
    {
        // Init. packet information vector
        QVector<QVariant> info = EmptyDataPacket();

        // Extract information to packet vector
        info.insert(kHeader,
                    QVariant(data.at(kHeader)));
        info.insert(kTeamID,
                    QVariant(data.at(kTeamID).toInt()));
        info.insert(kPacketCount,
                    QVariant(data.at(kPacketCount).toInt()));
        info.insert(kMisionTime,
                    QVariant(data.at(kMisionTime).toUInt()));
        info.insert(kAltitude,
                    QVariant(data.at(kAltitude).toDouble()));
        info.insert(kBatteryVoltage,
                    QVariant(data.at(kBatteryVoltage).toDouble()));
        info.insert(kRelativeHumidity,
                    QVariant(data.at(kRelativeHumidity).toDouble()));
        info.insert(kUvRadiationIndex,
                    QVariant(data.at(kUvRadiationIndex).toDouble()));
        info.insert(kInternalTemp,
                    QVariant(data.at(kInternalTemp).toDouble()));
        info.insert(kExternalTemp,
                    QVariant(data.at(kExternalTemp).toDouble()));
        info.insert(kAtmPressure,
                    QVariant(data.at(kAtmPressure).toDouble()));
        info.insert(kGpsTime,
                    QVariant(data.at(kGpsTime).toUInt()));
        info.insert(kGpsAltitude,
                    QVariant(data.at(kGpsAltitude).toDouble()));
        info.insert(kGpsVelocity,
                    QVariant(data.at(kGpsVelocity).toDouble()));
        info.insert(kGpsAltitude,
                    QVariant(data.at(kGpsAltitude).toDouble()));
        info.insert(kGpsLatitude,
                    QVariant(data.at(kGpsLatitude).toDouble()));
        info.insert(kGpsLongitude,
                    QVariant(data.at(kGpsLongitude).toDouble()));
        info.insert(kGpsSatelliteCount,
                    QVariant(data.at(kGpsSatelliteCount)));
        info.insert(kAccelerometerX,
                    QVariant(data.at(kAccelerometerX).toFloat()));
        info.insert(kAccelerometerY,
                    QVariant(data.at(kAccelerometerY).toFloat()));
        info.insert(kAccelerometerZ,
                    QVariant(data.at(kAccelerometerZ).toFloat()));
        info.insert(kGyroscopeX,
                    QVariant(data.at(kGyroscopeX).toFloat()));
        info.insert(kGyroscopeY,
                    QVariant(data.at(kGyroscopeY).toFloat()));
        info.insert(kGyroscopeZ,
                    QVariant(data.at(kGyroscopeZ).toFloat()));
        info.insert(kChecksumCode,
                    QVariant(data.at(kChecksumCode).toUInt()));

        // If current packet mision time is less than last packet
        // a satellite reset ocurred
        if (missionTime() > info.at(kMisionTime).toUInt())
            emit satelliteReset();

        // Update current packet
        m_data = info;
        emit dataParsed();
    }
}

void DataParser::saveCsvData() {
    if (csvLoggingEnabled()) {

    }
}

