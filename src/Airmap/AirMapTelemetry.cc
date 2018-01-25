/****************************************************************************
 *
 *   (c) 2009-2016 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "AirMapTelemetry.h"

AirMapTelemetry::AirMapTelemetry(AirMapSharedState& shared)
: _shared(shared)
{
}

void AirMapTelemetry::vehicleMavlinkMessageReceived(const mavlink_message_t& message)
{
    switch (message.msgid) {
    case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
        _handleGlobalPositionInt(message);
        break;
    case MAVLINK_MSG_ID_GPS_RAW_INT:
        _handleGPSRawInt(message);
        break;
    }

}

bool AirMapTelemetry::isTelemetryStreaming() const
{
    return _state == State::Streaming;
}

void AirMapTelemetry::_handleGPSRawInt(const mavlink_message_t& message)
{
    if (!isTelemetryStreaming()) {
        return;
    }

    mavlink_gps_raw_int_t gps_raw;
    mavlink_msg_gps_raw_int_decode(&message, &gps_raw);

    if (gps_raw.eph == UINT16_MAX) {
        _lastHdop = 1.f;
    } else {
        _lastHdop = gps_raw.eph / 100.f;
    }
}

void AirMapTelemetry::_handleGlobalPositionInt(const mavlink_message_t& message)
{
    if (!isTelemetryStreaming()) {
        return;
    }

    mavlink_global_position_int_t globalPosition;
    mavlink_msg_global_position_int_decode(&message, &globalPosition);

    Telemetry::Position position{
        milliseconds_since_epoch(Clock::universal_time()),
        (double) globalPosition.lat / 1e7,
        (double) globalPosition.lon / 1e7,
        (float) globalPosition.alt / 1000.f,
        (float) globalPosition.relative_alt / 1000.f,
        _lastHdop
    };
    Telemetry::Speed speed{
        milliseconds_since_epoch(Clock::universal_time()),
        globalPosition.vx / 100.f,
        globalPosition.vy / 100.f,
        globalPosition.vz / 100.f
    };

    Flight flight;
    flight.id = _flightID.toStdString();
    _shared.client()->telemetry().submit_updates(flight, _key,
        {Telemetry::Update{position}, Telemetry::Update{speed}});

}

void AirMapTelemetry::startTelemetryStream(const QString& flightID)
{
    if (_state != State::Idle) {
        qCWarning(AirMapManagerLog) << "Not starting telemetry: not in idle state:" << (int)_state;
        return;
    }

    qCInfo(AirMapManagerLog) << "Starting Telemetry stream with flightID" << flightID;

    _state = State::StartCommunication;
    _flightID = flightID;

    Flights::StartFlightCommunications::Parameters params;
    params.authorization = _shared.loginToken().toStdString();
    params.id = _flightID.toStdString();
    std::weak_ptr<LifetimeChecker> isAlive(_instance);
    _shared.client()->flights().start_flight_communications(params, [this, isAlive](const Flights::StartFlightCommunications::Result& result) {
        if (!isAlive.lock()) return;
        if (_state != State::StartCommunication) return;

        if (result) {
            _key = result.value().key;
            _state = State::Streaming;
        } else {
            _state = State::Idle;
            QString description = QString::fromStdString(result.error().description() ? result.error().description().get() : "");
            emit error("Failed to start telemetry streaming",
                    QString::fromStdString(result.error().message()), description);
        }
    });
}

void AirMapTelemetry::stopTelemetryStream()
{
    if (_state == State::Idle) {
        return;
    }
    qCInfo(AirMapManagerLog) << "Stopping Telemetry stream with flightID" << _flightID;

    _state = State::EndCommunication;
    Flights::EndFlightCommunications::Parameters params;
    params.authorization = _shared.loginToken().toStdString();
    params.id = _flightID.toStdString();
    std::weak_ptr<LifetimeChecker> isAlive(_instance);
    _shared.client()->flights().end_flight_communications(params, [this, isAlive](const Flights::EndFlightCommunications::Result& result) {
        Q_UNUSED(result);
        if (!isAlive.lock()) return;
        if (_state != State::EndCommunication) return;

        _key = "";
        _state = State::Idle;
    });
}
