import mqtt from 'mqtt';
import type { FireReport } from '../types';

// Your HiveMQ Cluster Configuration
const MQTT_CONFIG = {
  brokerUrl: 'wss://259353f6c5704a35aeb3dff107a0ab04.s1.eu.hivemq.cloud:8884/mqtt',
  username: 'Staferb',  // <-- put your actual username here
  password: 'EspWebDash@32',  // <-- put your actual password here
  topic: 'staferb/web_alerts'     // You can customize this if needed
};

let client: mqtt.MqttClient | null = null;

export const initializeMQTT = () => {
  if (!client) {
    client = mqtt.connect(MQTT_CONFIG.brokerUrl, {
      username: MQTT_CONFIG.username,
      password: MQTT_CONFIG.password,
      reconnectPeriod: 5000,
      connectTimeout: 8000
    });

    client.on('connect', () => {
      console.log('✅ Connected to HiveMQ (Your Cluster)');
    });

    client.on('error', (err) => {
      console.error('❌ MQTT Connection Error:', err);
    });
  }
  return client;
};

export const sendFireAlert = async (formData: FireReport) => {
  if (!client) {
    client = initializeMQTT();
  }

  const message = {
    command: 'fire_alert',
    payload: {
      fireType: formData.fireType,
      fireIntensity: formData.fireIntensity,
      verified: formData.verified,
      user: formData.user,
      userID: formData.userID,
      stnID: formData.stnID,
      latitude: formData.latitude,
      longitude: formData.longitude,
      date: formData.date,
      time: formData.time
    }
  };

  return new Promise((resolve, reject) => {
    client!.publish(MQTT_CONFIG.topic, JSON.stringify(message), { qos: 1 }, (err) => {
      if (err) {
        console.error('❌ Publish failed:', err);
        reject(err);
      } else {
        console.log('📡 Published:', message);
        resolve(message);
      }
    });
  });
};
