import React, { useState, useEffect, useCallback, useRef } from 'react';
import { Flame, MapPin, AlertTriangle, Send, Droplet, Thermometer as ThermometerHot, Camera } from 'lucide-react';
import Map, { NavigationControl, ScaleControl, GeolocateControl, FullscreenControl, AttributionControl, Marker, Popup } from 'react-map-gl';
import toast from 'react-hot-toast';
import Modal from 'react-modal';
import Lottie from 'lottie-react';
import fireAnimation from '../assets/fire-animation.json';
import { formatDDMCoordinates, getCurrentTimestamp } from '../utils/coordinates';
import { initializeMQTT, sendFireAlert } from '../utils/mqtt';
import type { FireReport, Coordinates } from '../types';

Modal.setAppElement('#root');

const FireMarker = ({ intensity = 1 }: { intensity?: number }) => {
  const baseSize = 48;
  const sizeIncrement = 24;
  const size = baseSize + (intensity - 1) * sizeIncrement;
  
  return (
    <div style={{ 
      width: size, 
      height: size, 
      marginLeft: -size/2, 
      marginTop: -size,
      cursor: 'pointer',
      pointerEvents: 'auto',
      transition: 'all 0.3s ease'
    }}>
      <Lottie 
        animationData={fireAnimation} 
        loop={true}
        autoplay={true}
        style={{ width: '100%', height: '100%' }}
      />
    </div>
  );
};

const FireReportForm: React.FC = () => {
  const [formData, setFormData] = useState<Partial<FireReport>>({
    fireType: 'A',
    fireIntensity: '1',
    verified: true,
    stnID: 'W/D',
    user: '',
    userID: '',
    ...getCurrentTimestamp()
  });
  
  const [userLocation, setUserLocation] = useState<Coordinates | null>(null);
  const [mapLoaded, setMapLoaded] = useState(false);
  const [mapInstance, setMapInstance] = useState<mapboxgl.Map | null>(null);
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [messageDetails, setMessageDetails] = useState<string>('');
  const [popupInfo, setPopupInfo] = useState<{location: Coordinates, isOpen: boolean} | null>(null);
  const geolocateControlRef = useRef<any>(null);
  const hoverTimeoutRef = useRef<NodeJS.Timeout | null>(null);

  useEffect(() => {
    initializeMQTT();
    return () => {
      if (hoverTimeoutRef.current) {
        clearTimeout(hoverTimeoutRef.current);
      }
    };
  }, []);

  const triggerSOS = async () => {
    try {
      const response = await fetch('http://localhost:5000/trigger-sos', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        credentials: 'include'
      });
      
      if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
      
      const data = await response.json();
      
      if (data.status === 'success') {
        toast.success('SOS alert sent successfully!', {
          /* autoClose: 10000  */// or duration/timeOut depending on your library
        });
      } else {
        throw new Error(data.message || 'Failed to send SOS');
      }
    } catch (error) {
      toast.error(`SOS failed: ${error.message}`);
      console.error('SOS error:', error);
    }
  };

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    
    if (!userLocation) {
      toast.error('Please enable location services first');
      return;
    }
    
    const { latitude, longitude } = formatDDMCoordinates(userLocation.lat, userLocation.lng);
    
    const payload: FireReport = {
      ...formData as FireReport,
      latitude,
      longitude
    };

    try {
      const message = await toast.promise(
        sendFireAlert(payload),
        {
          loading: 'Sending emergency alert...',
          success: 'Emergency alert sent successfully!',
          error: 'Failed to send emergency alert'
        }
      );

      setMessageDetails(JSON.stringify(message, null, 2));
      setIsModalOpen(true);
      await triggerSOS();
    } catch (error) {
      console.error('Error sending fire alert:', error);
    }
  };

  const handleGeolocate = useCallback((e: any) => {
    const newLocation = {
      lat: e.coords.latitude,
      lng: e.coords.longitude
    };
    setUserLocation(newLocation);
    setPopupInfo({ location: newLocation, isOpen: false });
  }, []);

  const handleMapLoad = useCallback((event: mapboxgl.MapboxEvent) => {
    const map = event.target;
    setMapInstance(map);
    setMapLoaded(true);
    map.resize();
  }, []);

  const handleMarkerMouseEnter = useCallback((location: Coordinates) => {
    if (hoverTimeoutRef.current) {
      clearTimeout(hoverTimeoutRef.current);
    }
    setPopupInfo({ location, isOpen: false });
    hoverTimeoutRef.current = setTimeout(() => {
      setPopupInfo({ location, isOpen: true });
    }, 300);
  }, []);

  const handleMarkerMouseLeave = useCallback(() => {
    if (hoverTimeoutRef.current) {
      clearTimeout(hoverTimeoutRef.current);
    }
    if (popupInfo && !popupInfo.isOpen) {
      setPopupInfo(null);
    }
  }, [popupInfo]);

  const handleMarkerClick = useCallback((e: any, location: Coordinates) => {
    e.originalEvent.stopPropagation();
    setPopupInfo({ location, isOpen: true });
  }, []);

  const handlePopupClose = useCallback(() => {
    setPopupInfo(null);
  }, []);

  const handleIntensityChange = useCallback((e: React.ChangeEvent<HTMLInputElement>) => {
    const newIntensity = e.target.value as FireReport['fireIntensity'];
    setFormData(prev => ({
      ...prev,
      fireIntensity: newIntensity
    }));
  }, []);

  const handleRecognizeFace = async () => {
    try {
      const response = await fetch('http://localhost:5000/recognize', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        credentials: 'include'
      });
      
      if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
      
      const data = await response.json();
      
      if (data.status === 'success') {
        toast.success(data.message);
        setFormData(prev => ({
          ...prev,
          user: data.data.name || '',
          userID: data.data.id || ''
        }));
      } else {
        throw new Error(data.message || 'Unknown error');
      }
    } catch (error) {
      toast.error(`Recognition failed: ${error.message}`);
      console.error('Recognition error:', error);
    }
  };

  if (!import.meta.env.VITE_MAPBOX_ACCESS_TOKEN) {
    return (
      <div className="bg-red-500/10 border border-red-500/20 rounded-lg p-4 text-red-500">
        Error: Mapbox access token is not configured.
      </div>
    );
  }

  return (
    <div className="bg-[#1A1F2E] rounded-xl shadow-xl overflow-hidden">
      <div className="p-6 border-b border-gray-700 flex justify-between items-center">
        <h2 className="text-2xl font-bold flex items-center gap-2">
          <Flame className="text-red-500" />
          Fire Report System
        </h2>
        <button 
          onClick={triggerSOS}
          className="flex items-center gap-2 px-4 py-2 bg-red-500 hover:bg-red-600 text-white rounded-lg transition-colors shadow-md hover:shadow-red-500/20"
        >
          <AlertTriangle className="w-5 h-5" />
          SOS Alert
        </button>
      </div>

      <form onSubmit={handleSubmit} className="p-6 space-y-8">
        <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
          <div className="space-y-6">
            <div>
              <label className="block text-sm font-medium text-gray-300 mb-3 flex items-center gap-2">
                <ThermometerHot className="w-4 h-4 text-orange-500" />
                Fire Type
              </label>
              <div className="grid grid-cols-4 gap-2">
                {['A', 'B', 'C', 'D'].map((type) => (
                  <button
                    key={type}
                    type="button"
                    onClick={() => setFormData({ ...formData, fireType: type as FireReport['fireType'] })}
                    className={`p-4 rounded-lg text-center transition-all ${
                      formData.fireType === type
                        ? 'bg-red-500 text-white shadow-lg shadow-red-500/30'
                        : 'bg-gray-700/50 text-gray-300 hover:bg-gray-700'
                    }`}
                  >
                    {type}
                  </button>
                ))}
              </div>
            </div>

            <div>
              <label className="block text-sm font-medium text-gray-300 mb-3 flex items-center gap-2">
                <Droplet className="w-4 h-4 text-blue-500" />
                Fire Intensity
              </label>
              <input
                type="range"
                min="1"
                max="4"
                value={formData.fireIntensity}
                onChange={handleIntensityChange}
                className="w-full h-2 bg-gray-700 rounded-lg appearance-none cursor-pointer"
              />
              <div className="flex justify-between mt-2">
                {[1, 2, 3, 4].map((num) => (
                  <div key={num} className="flex flex-col items-center">
                    <span className={`text-sm ${
                      parseInt(formData.fireIntensity!) === num 
                        ? 'text-red-500' 
                        : 'text-gray-400'
                    }`}>
                      {num}
                    </span>
                    <div className={`w-1 h-1 rounded-full mt-1 ${
                      parseInt(formData.fireIntensity!) === num 
                        ? 'bg-red-500' 
                        : 'bg-gray-600'
                    }`} />
                  </div>
                ))}
              </div>
            </div>

            <div className="space-y-3">
              <label className="block text-sm font-medium text-gray-300">
                Operator Credentials
              </label>
              <input
                type="text"
                placeholder="Username"
                value={formData.user}
                onChange={(e) => setFormData({ ...formData, user: e.target.value })}
                className="w-full p-3 bg-gray-700/50 rounded-lg text-white placeholder-gray-400 focus:outline-none focus:ring-2 focus:ring-red-500/50"
              />
              <input
                type="text"
                placeholder="User ID"
                value={formData.userID}
                onChange={(e) => setFormData({ ...formData, userID: e.target.value })}
                className="w-full p-3 bg-gray-700/50 rounded-lg text-white placeholder-gray-400 focus:outline-none focus:ring-2 focus:ring-red-500/50"
              />
              <button
                type="button"
                onClick={handleRecognizeFace}
                className="w-full p-3 bg-blue-500 hover:bg-blue-600 rounded-lg text-white transition-colors flex items-center justify-center gap-2"
              >
                <Camera className="w-5 h-5" />
                Recognize Face
              </button>
            </div>
          </div>

          <div className="space-y-6">
            <div>
              <label className="block text-sm font-medium text-gray-300 mb-3 flex items-center gap-2">
                <MapPin className="w-4 h-4 text-emerald-500" />
                Location
              </label>
              <div className="h-[300px] bg-gray-700/50 rounded-lg overflow-hidden relative">
                {!mapLoaded && (
                  <div className="absolute inset-0 flex items-center justify-center bg-gray-800/50 z-10">
                    <div className="animate-pulse text-gray-400">Loading map...</div>
                  </div>
                )}
                <Map
                  mapboxAccessToken={import.meta.env.VITE_MAPBOX_ACCESS_TOKEN}
                  initialViewState={{
                    latitude: 22.5767,
                    longitude: 88.2067,
                    zoom: 12,
                    bearing: 0,
                    pitch: 45
                  }}
                  style={{ width: '100%', height: '100%' }}
                  mapStyle="mapbox://styles/mapbox/satellite-streets-v12"
                  onLoad={handleMapLoad}
                  attributionControl={false}
                  antialias={true}
                  onClick={() => setPopupInfo(null)}
                >
                  <NavigationControl position="top-right" />
                  <ScaleControl position="bottom-left" unit="metric" />
                  <GeolocateControl
                    ref={geolocateControlRef}
                    position="top-right"
                    trackUserLocation={true}
                    showAccuracyCircle={false}
                    onGeolocate={handleGeolocate}
                  />
                  <FullscreenControl position="top-right" />
                  <AttributionControl compact={true} />

                  {userLocation && (
                    <>
                      <Marker
                        longitude={userLocation.lng}
                        latitude={userLocation.lat}
                        onClick={(e) => handleMarkerClick(e, userLocation)}
                        onMouseEnter={() => handleMarkerMouseEnter(userLocation)}
                        onMouseLeave={handleMarkerMouseLeave}
                      >
                        <FireMarker intensity={parseInt(formData.fireIntensity || '1')} />
                      </Marker>

                      {popupInfo && popupInfo.isOpen && (
                        <Popup
                          longitude={userLocation.lng}
                          latitude={userLocation.lat}
                          anchor="bottom"
                          onClose={handlePopupClose}
                          closeButton={false}
                          closeOnClick={false}
                          className="fire-popup"
                        >
                          <div className="p-2 text-sm min-w-[150px]">
                            <div className="font-bold mb-1">Fire Details</div>
                            <div>Type: {formData.fireType}</div>
                            <div>Intensity: {formData.fireIntensity}</div>
                            <div className="text-gray-500 text-xs mt-1">
                              {formatDDMCoordinates(userLocation.lat, userLocation.lng).latitude}, 
                              {formatDDMCoordinates(userLocation.lat, userLocation.lng).longitude}
                            </div>
                          </div>
                        </Popup>
                      )}
                    </>
                  )}
                </Map>
              </div>
              {userLocation && (
                <div className="mt-2 text-sm text-gray-400 flex items-center gap-2">
                  <MapPin className="w-4 h-4" />
                  {formatDDMCoordinates(userLocation.lat, userLocation.lng).latitude}, 
                  {formatDDMCoordinates(userLocation.lat, userLocation.lng).longitude}
                </div>
              )}
            </div>

            <div className="bg-gray-800/50 p-4 rounded-lg border border-gray-700/50">
              <h3 className="text-sm font-medium text-gray-300 mb-3 flex items-center gap-2">
                <AlertTriangle className="w-4 h-4 text-yellow-500" />
                Current Status
              </h3>
              <div className="space-y-2 text-sm">
                <div className="flex items-center justify-between">
                  <span className="text-gray-400">Station ID:</span>
                  <span className="text-white">{formData.stnID}</span>
                </div>
                <div className="flex items-center justify-between">
                  <span className="text-gray-400">Date:</span>
                  <span className="text-white">{formData.date}</span>
                </div>
                <div className="flex items-center justify-between">
                  <span className="text-gray-400">Time:</span>
                  <span className="text-white">{formData.time}</span>
                </div>
              </div>
            </div>
          </div>
        </div>

        <button
          type="submit"
          disabled={!userLocation}
          className={`w-full py-4 rounded-lg transition-all flex items-center justify-center gap-2 shadow-lg ${
            userLocation 
              ? 'bg-gradient-to-r from-red-500 to-red-600 text-white hover:from-red-600 hover:to-red-700'
              : 'bg-gray-600 text-gray-400 cursor-not-allowed'
          }`}
        >
          <Send className="w-5 h-5" />
          Submit Emergency Report
        </button>
      </form>

      <Modal
        isOpen={isModalOpen}
        onRequestClose={() => setIsModalOpen(false)}
        className="absolute top-1/2 left-1/2 transform -translate-x-1/2 -translate-y-1/2 bg-[#1A1F2E] p-6 rounded-xl shadow-xl max-w-lg w-full"
        overlayClassName="fixed inset-0 bg-black/50"
      >
        <h3 className="text-xl font-bold mb-4 text-white">Message Details</h3>
        <pre className="bg-gray-800/50 p-4 rounded-lg overflow-auto max-h-96 text-sm text-gray-300">
          {messageDetails}
        </pre>
        <button
          onClick={() => setIsModalOpen(false)}
          className="mt-4 w-full bg-red-500 text-white py-2 rounded-lg hover:bg-red-600 transition-colors"
        >
          Close
        </button>
      </Modal>
    </div>
  );
};

export default FireReportForm;
