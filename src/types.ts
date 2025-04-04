export interface FireReport {
  fireType: 'A' | 'B' | 'C' | 'D';
  fireIntensity: '1' | '2' | '3' | '4';
  verified: boolean;
  user: string;
  userID: string;
  stnID: string;
  latitude: string;
  longitude: string;
  date: string;
  time: string;
}

export interface Coordinates {
  lat: number;
  lng: number;
}