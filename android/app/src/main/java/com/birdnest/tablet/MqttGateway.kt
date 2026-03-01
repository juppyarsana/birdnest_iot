package com.birdnest.tablet

import android.content.Context
import android.provider.Settings
import android.util.Log
import org.eclipse.paho.client.mqttv3.IMqttActionListener
import org.eclipse.paho.client.mqttv3.IMqttToken
import org.eclipse.paho.client.mqttv3.MqttAsyncClient
import org.eclipse.paho.client.mqttv3.MqttConnectOptions
import org.eclipse.paho.client.mqttv3.MqttException
import org.eclipse.paho.client.mqttv3.MqttMessage
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence
import java.util.concurrent.atomic.AtomicBoolean

class MqttGateway(private val context: Context) {

    private val isConnecting = AtomicBoolean(false)
    @Volatile
    private var client: MqttAsyncClient? = null

    private val defaultControllerBaseTopic = "birdnest/controllers/birdnest-5C008B40C86C"

    private fun prefs() = context.getSharedPreferences("birdnest", Context.MODE_PRIVATE)

    fun hasPassword(): Boolean {
        val password = prefs().getString("mqtt_password", "") ?: ""
        return password.isNotBlank()
    }

    fun setPassword(password: String) {
        prefs().edit().putString("mqtt_password", password).apply()
        resetConnection()
    }

    fun setUsername(username: String) {
        prefs().edit().putString("mqtt_username", username).apply()
        resetConnection()
    }

    fun setBroker(host: String, port: Int) {
        prefs().edit().putString("mqtt_host", host).putInt("mqtt_port", port).apply()
        resetConnection()
    }

    fun setControllerBaseTopic(baseTopic: String) {
        prefs().edit().putString("controller_base_topic", baseTopic).apply()
    }

    private fun resetConnection() {
        disconnect()
        client = null
        isConnecting.set(false)
    }

    private fun brokerUri(): String {
        val host = prefs().getString("mqtt_host", "192.168.1.4") ?: "192.168.1.4"
        val port = prefs().getInt("mqtt_port", 1883)
        return "tcp://$host:$port"
    }

    private fun clientId(): String {
        val androidId = Settings.Secure.getString(context.contentResolver, Settings.Secure.ANDROID_ID) ?: "unknown"
        return "birdnest-tablet-$androidId"
    }

    private fun connectOptions(): MqttConnectOptions {
        val opts = MqttConnectOptions()
        opts.isAutomaticReconnect = true
        opts.isCleanSession = true
        opts.connectionTimeout = 10
        opts.keepAliveInterval = 30

        val username = prefs().getString("mqtt_username", "birdnest") ?: "birdnest"
        val password = prefs().getString("mqtt_password", "") ?: ""
        if (username.isNotBlank()) {
            opts.userName = username
        }
        if (password.isNotBlank()) {
            opts.password = password.toCharArray()
        }
        return opts
    }

    fun ensureConnected(onReady: (() -> Unit)? = null) {
        val existing = client
        if (existing != null && existing.isConnected) {
            onReady?.invoke()
            return
        }
        if (!isConnecting.compareAndSet(false, true)) return

        try {
            val uri = brokerUri()
            val c = MqttAsyncClient(uri, clientId(), MemoryPersistence())
            client = c

            c.connect(connectOptions(), null, object : IMqttActionListener {
                override fun onSuccess(asyncActionToken: IMqttToken?) {
                    isConnecting.set(false)
                    Log.i("BirdnestMqtt", "Connected to $uri")
                    onReady?.invoke()
                }

                override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                    isConnecting.set(false)
                    Log.e("BirdnestMqtt", "Connect failed", exception)
                }
            })
        } catch (e: MqttException) {
            isConnecting.set(false)
            Log.e("BirdnestMqtt", "Connect exception", e)
        }
    }

    fun publish(topic: String, payload: String, qos: Int = 0, retained: Boolean = false) {
        ensureConnected {
            val c = client ?: return@ensureConnected
            if (!c.isConnected) return@ensureConnected
            try {
                val msg = MqttMessage(payload.toByteArray(Charsets.UTF_8))
                msg.qos = qos
                msg.isRetained = retained
                c.publish(topic, msg)
                Log.i("BirdnestMqtt", "Publish $topic $payload")
            } catch (e: Exception) {
                Log.e("BirdnestMqtt", "Publish failed", e)
            }
        }
    }

    fun disconnect() {
        val c = client ?: return
        try {
            if (c.isConnected) {
                c.disconnect()
            }
        } catch (e: Exception) {
            Log.w("BirdnestMqtt", "Disconnect failed", e)
        }
    }

    fun resolveLightCommandTopic(): String {
        val base = prefs().getString("controller_base_topic", defaultControllerBaseTopic) ?: defaultControllerBaseTopic
        return "$base/cmd/light"
    }
}

