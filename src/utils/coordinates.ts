export const formatDDMCoordinates = (lat: number, lng: number): { latitude: string; longitude: string } => {
  const formatDDM = (coord: number, isLat: boolean) => {
    const absolute = Math.abs(coord);
    const degrees = Math.floor(absolute);
    const minutes = (absolute - degrees) * 60;
    const direction = isLat 
      ? coord >= 0 ? 'N' : 'S'
      : coord >= 0 ? 'E' : 'W';
    
    return `${degrees}°${minutes.toFixed(4)}'${direction}`;
  };

  return {
    latitude: formatDDM(lat, true),
    longitude: formatDDM(lng, false)
  };
};

export const getCurrentTimestamp = (): { date: string; time: string } => {
  const now = new Date();
  const date = now.toLocaleDateString('en-GB').replace(/\//g, '/');
  const time = now.toLocaleTimeString('en-GB');
  
  return { date, time };
};