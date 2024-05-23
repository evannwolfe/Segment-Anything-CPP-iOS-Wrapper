import SwiftUI

struct ContentView: View {
    @State private var selectedImage: UIImage?
    @State private var showingImagePicker = false
    @State private var processedImage: UIImage?

    var body: some View {
        NavigationView {
            VStack {
                if let selectedImage = selectedImage {
                    Image(uiImage: selectedImage)
                        .resizable()
                        .scaledToFit()
                }

                if let processedImage = processedImage {
                    Image(uiImage: processedImage)
                        .resizable()
                        .scaledToFit()
                }

                Button("Select Image") {
                    showingImagePicker = true
                }

                Button("Process Image") {
                    if let selectedImage = selectedImage {
                        processImage(image: selectedImage)
                    }
                }
            }
            .sheet(isPresented: $showingImagePicker) {
                ImagePicker(selectedImage: $selectedImage)
            }
            .navigationBarTitle("SAM Image Processing")
        }
    }

    func processImage(image: UIImage) {
        DispatchQueue.global(qos: .userInitiated).async {
            let wrapper = SAMWrapper()
            let result = wrapper.processImage(image)
            DispatchQueue.main.async {
                self.processedImage = result
            }
        }
    }
}
