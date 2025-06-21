const mqtt = require('mqtt');

// Replace with your actual HiveMQ Cloud credentials
const options = {
  username: 'Staferb',
  password: 'EspWebDash@32',
  reconnectPeriod: 5000,
  connectTimeout: 8000
};

const brokerUrl = 'wss://259353f6c5704a35aeb3dff107a0ab04.s1.eu.hivemq.cloud:8884/mqtt';
const topic = 'staferb/web_alerts';

const client = mqtt.connect(brokerUrl, options);

client.on('connect', () => {
  console.log('✅ Connected to HiveMQ broker');
  client.subscribe(topic, (err) => {
    if (err) {
      console.error('❌ Subscription error:', err);
    } else {
      console.log(`📡 Subscribed to topic: ${topic}`);
    }
  });
});

client.on('message', (topic, message) => {
  console.log(`\n📥 Message received on ${topic}:`);
  try {
    console.log(JSON.stringify(JSON.parse(message.toString()), null, 2));
  } catch (e) {
    console.log(message.toString());
  }
});

client.on('error', (err) => {
  console.error('❌ MQTT Connection Error:', err.message);
});
