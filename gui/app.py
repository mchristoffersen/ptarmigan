import flask
import json
import logging
import subprocess
import os
import signal
import threading
import glob
import tomllib

app = flask.Flask(__name__)

stateFile = "/home/lowres/ptarmigan/state.json"
logFile = "/home/lowres/ptarmigan/log.txt"
queryRadio_bin = "/home/lowres/ptarmigan/ctrl/query_radio"
queryGPS_bin = "/home/lowres/ptarmigan/ctrl/query_gps"
radar_bin = "/home/lowres/ptarmigan/ctrl/radar"
chirpConfigDir = "/home/lowres/ptarmigan/chirps"
dataDir = "/home/lowres/ptarmigan/data"

stateLock = threading.Lock()
logLock = threading.Lock()
queryLock = threading.Lock()
radarProc = None
logOffset = 0


def save_state(state):
    tmpStateFile = stateFile + ".%d.tmp" % os.getpid()
    with open(tmpStateFile, mode="w") as fd:
        json.dump(state, fd)

    os.replace(tmpStateFile, stateFile)


def load_state():
    with open(stateFile, mode="r") as fd:
        state = json.load(fd)

    return state


def get_state(var):
    state = load_state()
    if var in state.keys():
        return state[var]
    else:
        raise KeyError("No state variable: %s" % var)


def update_state(var, val):
    with stateLock:
        state = load_state()
        state[var] = val
        save_state(state)


def read_log():
    global logOffset
    with logLock:
        try:
            with open(logFile, mode="r") as fd:
                fd.seek(logOffset)
                log = fd.read()
                logOffset = fd.tell()
        except FileNotFoundError:
            return None

    return log if log else None


def initialize():
    global logOffset
    logOffset = (
        os.path.getsize(logFile)
        if os.path.exists(logFile)
        else 0
    )

    state = {
        "acquiring": False,
        "gpsStatus": None,
        "gpsFix": None,
        "gpsTime": None,
        "gpsLat": None,
        "gpsLon": None,
        "gpsHgt": None,
        "gpsSats": None,
        "radioStatus": None,
        "fileName": None,
        "rxTraces": None,
        "refClk": None,
        "refLock": None,
        "timeSrc": None,
        "lastTrace": None,
    }

    save_state(state)


def load_chirp_configs():
    configs = {}
    for path in sorted(
        glob.glob(chirpConfigDir + "/*.toml")
    ):
        with open(path, "rb") as f:
            config = tomllib.load(f)
        configs[
            os.path.basename(path)
            .replace(".toml", "")
            .replace("p", ".")
            .replace("_", "/")
        ] = config
    return configs


@app.route("/")
def index():
    configs = load_chirp_configs()
    return flask.render_template(
        "index.html", configs=configs
    )


def build_radar_args(config):
    # config units: MHz, percent of center freq, microseconds, dB rel. full scale
    chirp = config["chirp"][0]
    cf = chirp["center_frequency"] * 1e6
    bw = cf * chirp["bandwidth"] / 100
    spt = round(
        chirp["trace_length"] * config["sampling_freq"]
    )
    amp = 10 ** (chirp["power"] / 20)

    return [
        "--chirp-cf",
        "%g" % cf,
        "--chirp-bw",
        "%g" % bw,
        "--chirp-len",
        "%g" % (chirp["chirp_length"] * 1e-6),
        "--chirp-amp",
        "%g" % amp,
        "--chirp-prf",
        "%g" % config["prf"],
        "--spt",
        "%d" % spt,
        "--stack",
        "%d" % config["stacking"],
        "--dir",
        dataDir,
        "--state",
        stateFile,
    ]


@app.route("/start", methods=["POST"])
def start():
    global radarProc
    data = flask.request.get_json()
    xmit = data.get("xmit")
    acquiring = get_state("acquiring")
    configs = load_chirp_configs()

    if acquiring and radarProc is not None:
        message = "Acquisiiton already in progress"
    elif xmit not in configs:
        message = "No such chirp config: %s" % xmit
    else:
        args = build_radar_args(configs[xmit])
        message = (
            "Starting acquisition with waveform %s: %s"
            % (xmit, " ".join(args))
        )
        radarProc = subprocess.Popen([radar_bin] + args)

    print(message)

    return flask.jsonify(
        {
            "message": message,
        }
    )


@app.route("/stop", methods=["POST"])
def stop():
    global radarProc
    acquiring = get_state("acquiring")
    if not acquiring and radarProc is None:
        message = "No acquisition in progress"
    else:
        message = "Stopping acquisition"
        radarProc.terminate()
        try:
            radarProc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            radarProc.kill()
            radarProc.wait()
        radarProc = None
        update_state("acquiring", False)

    print(message)

    return flask.jsonify({"message": message})


def run_query(binary):
    with queryLock:
        subprocess.run([binary, "--state", stateFile])


@app.route("/queryRadio", methods=["POST"])
def queryRadio():
    acquiring = get_state("acquiring")
    if acquiring:
        message = "Cannot query radio during acquisition"
    else:
        message = "Querying radio"
        threading.Thread(
            target=run_query,
            args=(queryRadio_bin,),
            daemon=True,
        ).start()

    print(message)

    return flask.jsonify(
        {"status": "ok", "message": message}
    )


@app.route("/queryGPS", methods=["POST"])
def queryGPS():
    acquiring = get_state("acquiring")
    if acquiring:
        message = "Cannot query GPS during acquisition"
    else:
        message = "Querying GPS"
        threading.Thread(
            target=run_query,
            args=(queryGPS_bin,),
            daemon=True,
        ).start()

    print(message)

    return flask.jsonify(
        {"status": "ok", "message": message}
    )


@app.route("/state", methods=["GET"])
def getState():
    global radarProc
    state = load_state()
    state["message"] = read_log()
    if (
        radarProc is not None
        and radarProc.poll() is not None
    ):
        update_state("radioStatus", "error")
        radarProc = None
    return flask.jsonify(state)


if __name__ == "__main__":
    logging.getLogger("werkzeug").setLevel(logging.ERROR)
    initialize()
    app.run(
        host="0.0.0.0", port=80, threaded=False, debug=False
    )
    # app.run(debug=True)
