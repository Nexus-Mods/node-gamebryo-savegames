declare module 'gamebryo-savegame' {

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
    getScreenshot?: () => any;
    screenshot?: any;
  }

  interface Lib {
    Dimensions: Dimensions;
    GamebryoSaveGame: GamebryoSaveGame;
    create: (filePath: string, callback: (err: Error, save: GamebryoSaveGame) => void) => void;
  }

  function init(subpath: string): Lib;

  export default init;
}
