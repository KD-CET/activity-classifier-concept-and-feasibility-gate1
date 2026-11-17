"""A stand-in for paho.mqtt.client used ONLY by the tests: an in-process broker.
Messages are delivered immediately to every subscribed client. It checks the program logic
(topics, acks, resends, duplicates, Last Will), not real networking."""


class CallbackAPIVersion:
    VERSION2 = 2


_subs = []          # (topic filter, client)
_retained = {}


def reset():
    _subs.clear()
    _retained.clear()


def _match(filt, topic):
    f, t = filt.split("/"), topic.split("/")
    for i, part in enumerate(f):
        if part == "#":
            return True
        if i >= len(t) or (part != "+" and part != t[i]):
            return False
    return len(f) == len(t)


class MQTTMessage:
    def __init__(self, topic, payload, qos, retain):
        self.topic, self.qos, self.retain = topic, qos, retain
        self.payload = payload if isinstance(payload, bytes) else str(payload).encode()


class Client:
    def __init__(self, *args, client_id="", **kw):
        self.client_id = client_id
        self.on_connect = self.on_message = self.on_disconnect = None
        self._will = None
        self.connected = False

    def username_pw_set(self, u, p=None): pass
    def reconnect_delay_set(self, min_delay=1, max_delay=120): pass
    def will_set(self, topic, payload=None, qos=0, retain=False): self._will = (topic, payload, qos, retain)
    def connect_async(self, host, port=1883, keepalive=60): self._addr = (host, port)

    def loop_start(self):
        self.connected = True
        if self.on_connect:
            self.on_connect(self, None, {}, 0, None)

    def loop_stop(self): pass

    def subscribe(self, topic, qos=0):
        items = topic if isinstance(topic, list) else [(topic, qos)]
        for filt, _ in items:
            _subs.append((filt, self))
            for t, m in list(_retained.items()):
                if _match(filt, t) and self.on_message:
                    self.on_message(self, None, m)

    def publish(self, topic, payload=None, qos=0, retain=False):
        m = MQTTMessage(topic, payload, qos, retain)
        if retain:
            _retained[topic] = m
        for filt, c in list(_subs):
            if c.connected and _match(filt, topic) and c.on_message:
                c.on_message(c, None, m)

    def disconnect(self):
        self.connected = False
        _subs[:] = [(f, c) for f, c in _subs if c is not self]

    def drop(self):
        """Vanish without DISCONNECT: the broker publishes the Last Will."""
        self.disconnect()
        if self._will:
            self.publish(*self._will)
