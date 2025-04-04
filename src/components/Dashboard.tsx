import React from 'react';
import { Thermometer, Gauge, Droplet, Clock, MapPin, Settings } from 'lucide-react';

const Dashboard: React.FC = () => {
  return (
    <div className="space-y-6">
      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <MetricCard
          icon={<Thermometer className="w-5 h-5 text-orange-500" />}
          title="Temperature"
          value="42°C"
          color="bg-gradient-to-r from-orange-500/10 to-transparent"
        />
        <MetricCard
          icon={<Gauge className="w-5 h-5 text-blue-500" />}
          title="System Pressure"
          value="85 PSI"
          color="bg-gradient-to-r from-blue-500/10 to-transparent"
        />
        <MetricCard
          icon={<Droplet className="w-5 h-5 text-cyan-500" />}
          title="Retardant Level"
          value="75%"
          color="bg-gradient-to-r from-cyan-500/10 to-transparent"
        />
        <MetricCard
          icon={<Clock className="w-5 h-5 text-purple-500" />}
          title="Response Time"
          value="1.2s"
          color="bg-gradient-to-r from-purple-500/10 to-transparent"
        />
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
        <div className="bg-[#1A1F2E] rounded-xl p-6">
          <div className="flex items-center justify-between mb-4">
            <h2 className="text-xl font-semibold">Alert Messages</h2>
            <span className="px-3 py-1 bg-red-500/20 text-red-500 rounded-full text-sm">Emergency Alert</span>
          </div>
          <div className="bg-red-500/10 border border-red-500/20 rounded-lg p-4">
            <p className="text-sm text-gray-300">
              SOS Messages sent by common people will appear here with real-time updates
            </p>
          </div>
        </div>

        <div className="bg-[#1A1F2E] rounded-xl p-6">
          <div className="flex items-center justify-between mb-4">
            <h2 className="text-xl font-semibold">Detailed Device Log</h2>
            <button className="text-gray-400 hover:text-white transition-colors">
              <Settings className="w-5 h-5" />
            </button>
          </div>
          <div className="space-y-3">
            <LogEntry zone="Zone A" type="Electrical" intensity={7} status="Contained" retardant="150L" />
            <LogEntry zone="Zone B" type="Combustible" intensity={4} status="Extinguished" retardant="75L" />
          </div>
        </div>
      </div>
    </div>
  );
};

const MetricCard: React.FC<{
  icon: React.ReactNode;
  title: string;
  value: string;
  color: string;
}> = ({ icon, title, value, color }) => (
  <div className={`bg-[#1A1F2E] rounded-xl p-6 ${color}`}>
    <div className="flex items-center gap-3 mb-2">
      {icon}
      <span className="text-sm text-gray-400">{title}</span>
    </div>
    <div className="text-2xl font-bold">{value}</div>
  </div>
);

const LogEntry: React.FC<{
  zone: string;
  type: string;
  intensity: number;
  status: string;
  retardant: string;
}> = ({ zone, type, intensity, status, retardant }) => (
  <div className="flex items-center justify-between py-2 border-b border-gray-700/50">
    <div className="flex items-center gap-4">
      <span className="text-gray-400">{zone}</span>
      <span>{type}</span>
    </div>
    <div className="flex items-center gap-4">
      <div className="flex items-center gap-2">
        <div className="w-16 h-2 bg-gray-700 rounded-full overflow-hidden">
          <div 
            className="h-full bg-orange-500 rounded-full"
            style={{ width: `${(intensity / 10) * 100}%` }}
          />
        </div>
        <span className="text-sm">{intensity}</span>
      </div>
      <span className={`px-2 py-1 rounded text-sm ${
        status === 'Contained' ? 'bg-emerald-500/20 text-emerald-500' : 
        'bg-blue-500/20 text-blue-500'
      }`}>
        {status}
      </span>
      <span className="text-sm text-gray-400">{retardant}</span>
    </div>
  </div>
);

export default Dashboard;