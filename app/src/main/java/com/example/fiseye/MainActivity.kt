package com.example.fiseye

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.MediaStore
import android.util.Log
import android.widget.Button
import android.widget.ImageView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.RequiresApi
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream

class MainActivity : AppCompatActivity() {

    private lateinit var imageView: ImageView
    private lateinit var selectPhotoBtn: Button

    private val selectImageLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            val selectedUri: Uri? = result.data?.data
            selectedUri?.let { uri ->
                Log.d("MainActivity", "Image selected: $uri")
                val tempInputFilePath = copyUriToTempFile(this, uri)

                if (tempInputFilePath != null) {
                    Log.d("MainActivity", "Copied to temp input file: $tempInputFilePath")
                    val processedFilePath: String? = processImageFileInCpp(tempInputFilePath)

                    if (processedFilePath != null && processedFilePath.isNotEmpty()) {
                        Log.d("MainActivity", "C++ returned processed file path: $processedFilePath")
                        val processedFile = File(processedFilePath)
                        if (processedFile.exists()) {
                            imageView.setImageURI(Uri.fromFile(processedFile))
                            Log.d("MainActivity", "Displaying processed image from: $processedFilePath")
                        } else {
                            Log.e("MainActivity", "Processed file does not exist: $processedFilePath")
                            Toast.makeText(this, "Error: Processed file not found", Toast.LENGTH_LONG).show()
                            // imageView.setImageURI(null) // Optionally clear the image view
                        }
                        // Optionally, delete the temporary input file if no longer needed
                        // File(tempInputFilePath).delete()
                        // Also consider deleting processedFile if it's also temporary when appropriate
                    } else {
                        Log.e("MainActivity", "C++ processing failed or returned null/empty path.")
                        Toast.makeText(this, "Image processing failed", Toast.LENGTH_LONG).show()
                        // imageView.setImageURI(null) // Optionally clear the image view
                    }
                } else {
                    Log.e("MainActivity", "Failed to copy URI to temp file.")
                    Toast.makeText(this, "Failed to prepare image for processing", Toast.LENGTH_LONG).show()
                }
            }
        } else {
            Log.d("MainActivity", "Image selection cancelled or failed.")
        }
    }

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { isGranted: Boolean ->
        if (isGranted) {
            pickImage()
        } else {
            Toast.makeText(this, "Permission denied to read/write storage", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        imageView = findViewById(R.id.imageView)
        selectPhotoBtn = findViewById(R.id.selectPhotoBtn)

        selectPhotoBtn.setOnClickListener {
             triggerPhotoSelectionFromCpp()
        }
    }

    @RequiresApi(Build.VERSION_CODES.M)
    fun requestPickImageViaKotlin() {
        Log.d("MainActivity", "requestPickImageViaKotlin called from C++")
        val permission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            Manifest.permission.READ_MEDIA_IMAGES
        } else {
            Manifest.permission.READ_EXTERNAL_STORAGE
        }

        when {
            ContextCompat.checkSelfPermission(
                this,
                permission
            ) == PackageManager.PERMISSION_GRANTED -> {
                pickImage()
            }
            shouldShowRequestPermissionRationale(permission) -> {
                Toast.makeText(this, "Storage permission is needed to select photos.", Toast.LENGTH_LONG).show()
                requestPermissionLauncher.launch(permission)
            }
            else -> {
                requestPermissionLauncher.launch(permission)
            }
        }
    }

    private fun pickImage() {
        Log.d("MainActivity", "pickImage called")
        val intent = Intent(Intent.ACTION_PICK, MediaStore.Images.Media.EXTERNAL_CONTENT_URI)
        intent.type = "image/*"
        selectImageLauncher.launch(intent)
    }

    private fun copyUriToTempFile(context: Context, uri: Uri): String? {
        return try {
            val inputStream: InputStream? = context.contentResolver.openInputStream(uri)
            if (inputStream == null) {
                Log.e("MainActivity", "Failed to open input stream for URI: $uri")
                return null
            }
            val fileType = context.contentResolver.getType(uri)
            val fileExtension = if (fileType != null && fileType.startsWith("image/")) {
                "." + fileType.substringAfterLast('/') // e.g. .jpg, .png
            } else {
                ".tmp" // fallback extension
            }
            // Ensure the extension is one that C++ image libraries might expect (e.g. no special chars)
            val safeExtension = fileExtension.replace(Regex("[^A-Za-z0-9.]"), "")

            val tempFile = File.createTempFile("temp_input_image_", safeExtension, context.cacheDir)
            val outputStream = FileOutputStream(tempFile)
            inputStream.use { input ->
                outputStream.use { output ->
                    input.copyTo(output)
                }
            }
            tempFile.absolutePath
        } catch (e: Exception) {
            Log.e("MainActivity", "Error copying URI to temp file", e)
            null
        }
    }

    // --- Native Method Declarations ---
    external fun triggerPhotoSelectionFromCpp()
    external fun processImageFileInCpp(inputFilePath: String): String?


    companion object {
        init {
            System.loadLibrary("fiseye")
        }
    }
}
