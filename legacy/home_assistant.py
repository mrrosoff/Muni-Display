"""Home Assistant REST API client for washer/dryer state."""

import time
from datetime import datetime

import requests

WASHER_RUNNING = "binary_sensor.washer_running"
DRYER_RUNNING = "binary_sensor.dryer_running"
WASHER_AVG = "sensor.washer_avg_run_minutes"
DRYER_AVG = "sensor.dryer_avg_run_minutes"


def _get_state(base_url, token, entity_id, timeout):
    url = f"{base_url.rstrip('/')}/api/states/{entity_id}"
    headers = {"Authorization": f"Bearer {token}",
               "Connection": "close"}
    r = requests.get(url, headers=headers, timeout=timeout)
    r.raise_for_status()
    return r.json()


def _parse_iso_to_epoch(s):
    if not s:
        return None
    return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()


def _parse_avg_min(state_obj):
    try:
        return int(round(float(state_obj["state"])))
    except (KeyError, TypeError, ValueError):
        return None


def _appliance(running_obj, avg_obj):
    on = running_obj["state"] == "on"
    started_at = _parse_iso_to_epoch(running_obj.get("last_changed")) if on else None
    return {
        "on": on,
        "started_at": started_at,
        "avg_min": _parse_avg_min(avg_obj) if avg_obj else None,
    }


def fetch_laundry(base_url, token, timeout=(5, 10)):
    washer = _get_state(base_url, token, WASHER_RUNNING, timeout)
    dryer = _get_state(base_url, token, DRYER_RUNNING, timeout)
    try:
        washer_avg = _get_state(base_url, token, WASHER_AVG, timeout)
    except requests.RequestException:
        washer_avg = None
    try:
        dryer_avg = _get_state(base_url, token, DRYER_AVG, timeout)
    except requests.RequestException:
        dryer_avg = None
    return {
        "washer": _appliance(washer, washer_avg),
        "dryer": _appliance(dryer, dryer_avg),
        "fetched_at": time.time(),
    }
