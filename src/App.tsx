import React from 'react';
import FireReportForm from './components/FireReportForm';
import Dashboard from './components/Dashboard';
import { Bell } from 'lucide-react';
import { Toaster } from 'react-hot-toast';
import 'mapbox-gl/dist/mapbox-gl.css';


function App() {
  return (
    <div className="min-h-screen bg-[#0F1520] text-white">
      <Toaster position="top-right" />
      <header className="bg-[#1A1F2E] p-4 shadow-lg">
        <div className="max-w-7xl mx-auto flex items-center justify-between">
          <div className="flex items-center gap-2">
            <span className="text-2xl font-bold">FIRE<span className="text-red-500">LINX</span></span>
          </div>
          <div className="flex items-center gap-4">
            <span className="text-emerald-400 flex items-center gap-2">
              <span className="w-2 h-2 bg-emerald-400 rounded-full animate-pulse"></span>
              System Online
            </span>
            <button className="p-2 hover:bg-gray-700 rounded-full transition-colors">
              <Bell className="w-5 h-5" />
            </button>
          </div>
        </div>
      </header>

      <main className="max-w-7xl mx-auto p-6 space-y-6">
        <Dashboard />
        <FireReportForm />
      </main>
    </div>
  );
}

export default App;