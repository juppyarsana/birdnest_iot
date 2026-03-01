package com.birdnest.tablet

import android.annotation.SuppressLint
import android.os.Bundle
import android.webkit.JavascriptInterface
import android.webkit.WebChromeClient
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    private lateinit var webView: WebView
    private lateinit var mqtt: MqttGateway

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        mqtt = MqttGateway(this)
        webView = WebView(this)
        webView.webViewClient = WebViewClient()
        webView.webChromeClient = WebChromeClient()
        webView.settings.javaScriptEnabled = true
        webView.settings.domStorageEnabled = true
        webView.settings.useWideViewPort = true
        webView.settings.loadWithOverviewMode = true
        webView.addJavascriptInterface(Bridge(), "BirdnestAndroid")

        setContentView(webView)
        webView.loadUrl("file:///android_asset/www/index.html")

        onBackPressedDispatcher.addCallback(
            this,
            object : OnBackPressedCallback(true) {
                override fun handleOnBackPressed() {
                    if (webView.canGoBack()) {
                        webView.goBack()
                    } else {
                        isEnabled = false
                        onBackPressedDispatcher.onBackPressed()
                    }
                }
            }
        )
    }

    override fun onDestroy() {
        mqtt.disconnect()
        webView.removeJavascriptInterface("BirdnestAndroid")
        super.onDestroy()
    }

    inner class Bridge {
        @JavascriptInterface
        fun setMainLight(isOn: Boolean) {
            val topic = mqtt.resolveLightCommandTopic()
            val payload = """{"light":1,"state":$isOn}"""
            mqtt.publish(topic, payload, qos = 0, retained = false)
        }

        @JavascriptInterface
        fun hasMqttPassword(): Boolean {
            return mqtt.hasPassword()
        }

        @JavascriptInterface
        fun setMqttPassword(password: String) {
            mqtt.setPassword(password)
        }
    }
}
