import mqtt from 'mqtt';

// HiveMQ Cloud Configuration
const MQTT_CONFIG = {
  clusterId: '075450a6c27340a0bd939f7d516ec460',
  username: 'hivemq.webclient.1743685329123',
  password: '9ZG6qrP3H:A>y1;an@sK',
  topic: 'staferb/web_alerts'
};

const brokerUrl = `wss://${MQTT_CONFIG.clusterId}.s1.eu.hivemq.cloud:8884/mqtt`;

let client: mqtt.MqttClient | null = null;

export const initializeMQTT = () => {
  if (!client) {
    client = mqtt.connect(brokerUrl, {
      username: MQTT_CONFIG.username,
      password: MQTT_CONFIG.password,
      reconnectPeriod: 5000,
      connectTimeout: 8000
    });

    client.on('connect', () => {
      console.log('Connected to HiveMQ Cloud');
    });

    client.on('error', (err) => {
      console.error('Connection error:', err);
    });
  }
  return client;
};

export const sendFireAlert = async (formData: any) => {
  if (!client) {
    client = initializeMQTT();
  }

  const message = {
    command: 'fire_alert',
    payload: `${formData.fireType},${formData.fireIntensity},${formData.verified},${formData.user},${formData.userID},${formData.stnID},${formData.latitude},${formData.longitude},${formData.date},${formData.time}`
  };

  return new Promise((resolve, reject) => {
    client!.publish(MQTT_CONFIG.topic, JSON.stringify(message), { qos: 1 }, (err) => {
      if (err) {
        console.error('Publish error:', err);
        reject(err);
      } else {
        console.log('Published:', message);
        resolve(message);
      }
    });
  });
};