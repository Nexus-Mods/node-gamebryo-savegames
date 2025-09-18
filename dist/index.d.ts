export interface Dimensions {
  width: number;
  height: number;
}

export interface GamebryoSaveGame {
  new (filePath: string): GamebryoSaveGame;
  characterName: string;
  characterLevel: number;
  location: string;
  saveNumber: number;
  plugins: string[];
  creationTime: number;
  fileName: string;
  screenshotSize: Dimensions;
  playTime: string;
  getScreenshot?: () => any;
  screenshot?: any;
}


export function create(filePath: string, quick: boolean, callback: (err: Error, save: GamebryoSaveGame) => void): void;